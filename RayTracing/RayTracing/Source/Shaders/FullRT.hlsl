#include "PBR.hlsli"

static const float kConst = 1.0f; // Prevents singularity at d=0
static const float kLinear = 0.0f; // No artificial dampening
static const float kQuadratic = 1.0f; // Physical Inverse Square Law

// ===============================================================================================
// --- GLOBAL RESOURCES (Space 0) ---
// ===============================================================================================

// Group A: Outputs
RWTexture2D<float4> gOutColor : register(u0); // Final Color
RWTexture2D<float4> gOutAlbedo : register(u1); // Base Color
RWTexture2D<float4> gOutAlbedoSpecular : register(u2); // F0

// Group B: G-Buffer Data (Shifted down from u5)
RWTexture2D<float4> gOutNormal : register(u3);
RWTexture2D<float4> gOutEmissive : register(u4);
RWTexture2D<float2> gOutMaterial : register(u5);
RWTexture2D<float> gOutDepth : register(u6);

RaytracingAccelerationStructure gScene : register(t5);
StructuredBuffer<Light> gLights : register(t6);

cbuffer FrameCB : register(b0)
{
    float4x4 vpMatrix;
    float4x4 invViewProj;
    
    float3 viewPos;
    int numLights;
    
    int numPointLights;
    int frameCount;
    int shadowsEnabled;
    int reflectionsEnabled;
    
    int maxReflectionDepth;
    int maxTransparentDepth;
    int risCandidates;
    int shadowRays;
    
    float nearZ;
    float farZ;
    float2 _pad0;
};

// ===============================================================================================
// --- LOCAL RESOURCES (Space 1 - From SBT) ---
// ===============================================================================================

ByteAddressBuffer gIndices : register(t0, space1);
ByteAddressBuffer gVertices : register(t1, space1);


cbuffer MaterialCB : register(b0, space1)
{
    float4 gBaseColorFactor;
    float gMetalnessFactor;
    float gRoughnessFactor;
    float gAlphaCutoff;
    float gTransmissionFactor; // NEW
    
    float4 gEmissiveFactor;
    
    float gIOR; // NEW
    float gAttenuationDistance; // NEW
    float2 _Pad1;
    float4 gAttenuationColor; // NEW
};

Texture2D gAlbedoMap : register(t2, space1);
Texture2D gMetalnessMap : register(t3, space1);
Texture2D gRoughnessMap : register(t4, space1);
Texture2D gNormalMap : register(t5, space1);
Texture2D gEmissiveMap : register(t6, space1);

SamplerState gSampler : register(s0);

// ===============================================================================================
// --- PAYLOADS & STRUCTURES ---
// ===============================================================================================

struct RayPayload
{
    float3 color;
    uint reflectionDepth;
    uint transparentDepth;
    float hitT; // -1.0 if miss
    
    float3 dDdx; // Change in ray direction per pixel X
    float3 dDdy; // Change in ray direction per pixel Y
    
    float4 blendAlbedo;
    float3 blendDiffuse;
    float3 blendF0;
    float2 blendMaterial;
    
    float minDecalT;
};

struct ShadowPayload
{
    bool isVisible;
};

struct VertexAttributes
{
    float3 normal;
    float4 tangent;
    float2 uv;
};

// ===============================================================================================
// --- HELPER FUNCTIONS ---
// ===============================================================================================
// Helper: Apply Beer's Law for Volume Absorption
float3 ApplyVolumeAttenuation(float3 currentThroughput, float hitDistance, float3 attColor, float attDist)
{
    if (attDist >= 10000.0f)
        return currentThroughput;

    // Beer's Law: transmittance = exp(-sigma * distance)
    // sigma = -log(attenuationColor) / attenuationDistance
    float3 sigma = -log(max(attColor, 0.001f)) / max(attDist, 0.001f);
    float3 transmittance = exp(-sigma * hitDistance);

    return currentThroughput * transmittance;
}

void ComputeGradients(
    uint triangleIndex,
    float3 dDdx, float3 dDdy,
    float3 D, float t,
    out float2 dUVdx, out float2 dUVdy)
{
    // 1. Fetch raw Triangle Data (Positions & UVs)
    uint indexOffset = triangleIndex * 3 * 4;
    uint3 idx = gIndices.Load3(indexOffset);
    const uint stride = 56;

    float3 p0 = asfloat(gVertices.Load3(idx.x * stride));
    float3 p1 = asfloat(gVertices.Load3(idx.y * stride));
    float3 p2 = asfloat(gVertices.Load3(idx.z * stride));

    float2 uv0 = asfloat(gVertices.Load2(idx.x * stride + 24));
    float2 uv1 = asfloat(gVertices.Load2(idx.y * stride + 24));
    float2 uv2 = asfloat(gVertices.Load2(idx.z * stride + 24));

    // 2. Compute Surface Derivatives (Edges)
    float3 dp1 = p1 - p0;
    float3 dp2 = p2 - p0;
    float2 du1 = uv1 - uv0;
    float2 du2 = uv2 - uv0;

    float3 faceNormal = normalize(cross(dp1, dp2));
    // 3. Project Ray Differentials onto Surface Plane (dP/dx, dP/dy)
    // Formula: dP = t * dD - D * (dot(N, t*dD) / dot(N, D))
    float NdotD = dot(faceNormal, D);
    if (abs(NdotD) < 1e-6f)
        NdotD = (NdotD >= 0 ? 1e-6f : -1e-6f);
    float3 dPdx = t * dDdx - D * (dot(faceNormal, t * dDdx) / NdotD);
    float3 dPdy = t * dDdy - D * (dot(faceNormal, t * dDdy) / NdotD);

    // 4. Solve for UV Gradients
    // We want k1, k2 such that: dP = k1 * dp1 + k2 * dp2
    // Then dUV = k1 * du1 + k2 * du2
    // Using Least Squares / Dot products:
    float dot11 = dot(dp1, dp1);
    float dot12 = dot(dp1, dp2);
    float dot22 = dot(dp2, dp2);
    float det = dot11 * dot22 - dot12 * dot12;

    float invDet = (abs(det) < 1e-20f) ? 0.0f : 1.0f / det;

    float dot1Pdx = dot(dp1, dPdx);
    float dot2Pdx = dot(dp2, dPdx);
    float k1x = (dot22 * dot1Pdx - dot12 * dot2Pdx) * invDet;
    float k2x = (dot11 * dot2Pdx - dot12 * dot1Pdx) * invDet;
    dUVdx = k1x * du1 + k2x * du2;

    float dot1Pdy = dot(dp1, dPdy);
    float dot2Pdy = dot(dp2, dPdy);
    float k1y = (dot22 * dot1Pdy - dot12 * dot2Pdy) * invDet;
    float k2y = (dot11 * dot2Pdy - dot12 * dot1Pdy) * invDet;
    dUVdy = k1y * du1 + k2y * du2;
}
// --- Random Number Generator ---
uint initRand(uint val0, uint val1, uint backoff = 16)
{
    uint v0 = val0, v1 = val1, s0 = 0;
    for (uint n = 0; n < backoff; n++)
    {
        s0 += 0x9e3779b9;
        v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
        v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
    }
    return v0;
}

float nextRand(inout uint s)
{
    s = (1664525u * s + 1013904223u);
    return float(s & 0x00FFFFFF) / float(0x01000000);
}

float3 GetConeSample(inout uint seed, float3 L, float spreadAngle)
{
    float r1 = nextRand(seed);
    float r2 = nextRand(seed);

    float z = 1.0f - r2 * (1.0f - cos(spreadAngle));
    float phi = 6.2831853f * r1;
    float x = cos(phi) * sqrt(1.0f - z * z);
    float y = sin(phi) * sqrt(1.0f - z * z);

    float3 d = float3(x, y, z);

    // Create an orthonormal basis around L
    float3 up = abs(L.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, L));
    float3 bitangent = cross(L, tangent);

    // Transform d to the basis
    return d.x * tangent + d.y * bitangent + d.z * L;
}

float2 GetHitUV(uint triangleIndex, float3 bary)
{
    uint indexOffset = triangleIndex * 3 * 4;
    uint3 idx = gIndices.Load3(indexOffset);
    const uint stride = 56;

    // Fetch UVs (Offset 24 in your layout)
    float2 uv0 = asfloat(gVertices.Load2(idx.x * stride + 24));
    float2 uv1 = asfloat(gVertices.Load2(idx.y * stride + 24));
    float2 uv2 = asfloat(gVertices.Load2(idx.z * stride + 24));

    return uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
}

// --- Vertex Fetching ---
VertexAttributes GetHitSurface(uint triangleIndex, float3 bary)
{
    uint indexOffset = triangleIndex * 3 * 4;
    uint3 idx = gIndices.Load3(indexOffset);

    // Stride = 56 bytes (Pos(12) + Norm(12) + UV(8) + Tan(16) + Mat(8))
    const uint stride = 56;
    
    // Fetch Normals (Offset 12)
    float3 n0 = asfloat(gVertices.Load3(idx.x * stride + 12));
    float3 n1 = asfloat(gVertices.Load3(idx.y * stride + 12));
    float3 n2 = asfloat(gVertices.Load3(idx.z * stride + 12));
    float3 normal = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);

    // Fetch UVs (Offset 24)
    float2 uv0 = asfloat(gVertices.Load2(idx.x * stride + 24));
    float2 uv1 = asfloat(gVertices.Load2(idx.y * stride + 24));
    float2 uv2 = asfloat(gVertices.Load2(idx.z * stride + 24));
    float2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;

    // Fetch Tangents (Offset 32)
    float4 t0 = asfloat(gVertices.Load4(idx.x * stride + 32));
    float4 t1 = asfloat(gVertices.Load4(idx.y * stride + 32));
    float4 t2 = asfloat(gVertices.Load4(idx.z * stride + 32));
    float4 tangent = t0 * bary.x + t1 * bary.y + t2 * bary.z;

    VertexAttributes attr;
    attr.normal = normal;
    attr.uv = uv;
    attr.tangent = tangent;
    return attr;
}


// --- Normal Mapping ---
float3 CalculateNormal(float3 N, float4 tangent, float3 normalSample)
{
    if (dot(tangent.xyz, tangent.xyz) < 0.001f)
    {
        return N;
    }
    float3 tangentNormal = normalSample * 2.0f - 1.0f;

    float3 T = normalize(tangent.xyz - dot(tangent.xyz, N) * N);
    float3 B = cross(N, T) * tangent.w;
    float3x3 TBN = float3x3(T, B, N);

    return normalize(mul(tangentNormal, TBN));
}

float EvaluateLightImportance(Light light, float3 worldPos, float3 normal)
{
    float3 toLight = light.position.xyz - worldPos;
    float distSq = dot(toLight, toLight);
    float dist = sqrt(distSq);
    float3 L = toLight / dist;
    
    // Simple NdotL check (cull backfacing lights)
    float NdotL = max(dot(normal, L), 0.0f);
    
    // Attenuation (Inverse Square Law)
    float att = 1.0f / (kConst + kLinear * dist + kQuadratic * distSq);
    
    // Importance = Color Luminance * Attenuation * Angle
    // (Luminance = dot(color, float3(0.2126, 0.7152, 0.0722)))
    float luminance = dot(light.diffuseColor.rgb, float3(0.2126, 0.7152, 0.0722));
    
    return luminance * att * NdotL;
}

// ===============================================================================================
// --- SHADER ENTRIES ---
// ===============================================================================================

// 1. RAY GENERATION
[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dim = DispatchRaysDimensions().xy;

    float2 uv = (pixel + 0.5f) / float2(dim);
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    // Main Ray
    float4 target = mul(invViewProj, float4(ndc, 1.0f, 1.0f));
    float3 rayDir = normalize(target.xyz / target.w - viewPos);
    
    // Helper Rays (Differentials)
    float2 ndcRight = (uv + float2(1.0f / dim.x, 0)) * 2.0f - 1.0f;
    ndcRight.y = -ndcRight.y;
    float4 targetRight = mul(invViewProj, float4(ndcRight, 1.0f, 1.0f));
    float3 rayDirRight = normalize(targetRight.xyz / targetRight.w - viewPos);

    float2 ndcDown = (uv + float2(0, 1.0f / dim.y)) * 2.0f - 1.0f;
    ndcDown.y = -ndcDown.y;
    float4 targetDown = mul(invViewProj, float4(ndcDown, 1.0f, 1.0f));
    float3 rayDirDown = normalize(targetDown.xyz / targetDown.w - viewPos);

    RayDesc ray;
    ray.Origin = viewPos;
    ray.Direction = rayDir;
    ray.TMin = nearZ;
    ray.TMax = farZ;

    RayPayload payload;
    payload.color = float3(0, 0, 0);
    payload.reflectionDepth = 0;
    payload.transparentDepth = 0;
    payload.hitT = 0.0f;
    payload.dDdx = rayDirRight - rayDir;
    payload.dDdy = rayDirDown - rayDir;
    payload.blendAlbedo = float4(0, 0, 0, 0);
    payload.blendDiffuse = float3(0, 0, 0);
    payload.blendF0 = float3(0, 0, 0);
    payload.blendMaterial = float2(0, 0);
    payload.minDecalT = 1e20f;

    // Trace Primary Ray
    TraceRay(gScene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, ray, payload);

    gOutColor[pixel] = float4(payload.color, 1.0f);
    
    float depth = 1.0f;
    if (payload.hitT >= 0.0f)
    {
        // === HIT CASE ===
        // 1. Reconstruct World Position
        float3 hitWorldPos = ray.Origin + ray.Direction * payload.hitT;

        // 2. Project to Clip Space
        float4 clipPos = mul(vpMatrix, float4(hitWorldPos, 1.0f));

        // 3. Calculate Depth
        depth = clipPos.z / clipPos.w;
    }
    else
    {
        // === MISS CASE ===
        // Note: Albedo and F0 are written inside ClosestHit for the primary ray.
        // If it was a miss, we clear them here to be safe.
        //gOutDiffuse[pixel] = float4(0, 0, 0, 0);
        //gOutSpecular[pixel] = float4(0, 0, 0, 0);
        gOutAlbedo[pixel] = float4(0, 0, 0, 0);
        gOutAlbedoSpecular[pixel] = float4(0, 0, 0, 0);
        gOutNormal[pixel] = float4(0, 0, 0, 0);
        gOutEmissive[pixel] = float4(0, 0, 0, 0);
        gOutMaterial[pixel] = float2(0, 0);
    }
    
    gOutDepth[pixel] = depth;
}

// 2. MISS SHADERS
[shader("miss")]
void Miss(inout RayPayload payload)
{
    // Simple gradient sky
    float3 rayDir = WorldRayDirection();
    float t = 0.5 * (rayDir.y + 1.0);
    float3 sky = lerp(float3(0.3, 0.3, 0.3), float3(0.5, 0.7, 1.0), t);

    payload.color = sky; // Sky is diffuse for now    
    payload.hitT = -1.0f;
    payload.blendAlbedo = float4(sky, 1.0f);
    payload.blendDiffuse = float3(0, 0, 0);
    payload.blendF0 = float3(0, 0, 0);
    payload.blendMaterial = float2(0, 0);
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
    payload.isVisible = true;
}

// 3. CLOSEST HIT
//static const uint MAX_RECURSION_DEPTH = 2; // Keep low for performance

void DoShading(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr, bool isTransparent)
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint triangleIndex = PrimitiveIndex();
    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    bool isBackFace = (HitKind() == HIT_KIND_TRIANGLE_BACK_FACE);
    VertexAttributes vert = GetHitSurface(triangleIndex, bary);
    if (isBackFace)
    {
        vert.normal = -vert.normal;
        vert.tangent.w = -vert.tangent.w;
    }
    
    float2 dUVdx = 0;
    float2 dUVdy = 0;
    //if (payload.reflectionDepth == 0 && payload.transparentDepth == 0)
    {
        float3 viewDir = WorldRayDirection();
        float hitT = RayTCurrent();
        ComputeGradients(triangleIndex, payload.dDdx, payload.dDdy, viewDir, hitT, dUVdx, dUVdy);
    }
    
    

    // --- Material Sampling ---
    float4 albedoSample = gAlbedoMap.SampleGrad(gSampler, vert.uv, dUVdx, dUVdy);
    float3 albedo = albedoSample.rgb * gBaseColorFactor.rgb;
    float alpha = albedoSample.a * gBaseColorFactor.a;
    float metalness = gMetalnessMap.SampleGrad(gSampler, vert.uv, dUVdx, dUVdy).b * gMetalnessFactor;
    float roughness = gMetalnessMap.SampleGrad(gSampler, vert.uv, dUVdx, dUVdy).g * gRoughnessFactor;
    roughness = max(roughness, 0.0000000000000001f); // Prevent 0 roughness)
    float3 emissive = gEmissiveMap.SampleGrad(gSampler, vert.uv, dUVdx, dUVdy).rgb * gEmissiveFactor.rgb;
    float3 normalSample = gNormalMap.SampleGrad(gSampler, vert.uv, dUVdx, dUVdy).rgb;
    //float3 normalSample = gNormalMap.SampleLevel(gSampler, vert.uv, 0).rgb; // No gradients for normal map to avoid artifacts)
    float3 normal = CalculateNormal(normalize(vert.normal), vert.tangent, normalSample);    
    //float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
    float transmission = gTransmissionFactor;
    float F0_Dielectric = pow((gIOR - 1.0f) / (gIOR + 1.0f), 2.0f);
    float3 F0 = lerp(float3(F0_Dielectric, F0_Dielectric, F0_Dielectric), albedo, metalness);

    if (payload.minDecalT <= RayTCurrent())
    {
        float visibility = 1.0f - payload.blendAlbedo.a;        
        albedo = albedo * visibility + payload.blendAlbedo.rgb;
        metalness = metalness * visibility + payload.blendMaterial.y;
        roughness = roughness * visibility + payload.blendMaterial.x;
        F0 = F0 * visibility + payload.blendF0;
    }
    
    float3 worldPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 V = -WorldRayDirection();
    float NdotV = max(dot(normal, V), 0.0f);

    // --- Direct Lighting ---
    float3 directDiffuseIrradiance = float3(0, 0, 0); // Pure Light (No Albedo)
    float3 directSpecular = float3(0, 0, 0);
    
    float distToCamera = length(viewPos - worldPos);
    float bias = 0.001f + distToCamera * 0.002f;
    uint seed = initRand(pixel.x + frameCount * 17, pixel.y + frameCount * 31);
    
    
    
    [branch]
    if (shadowsEnabled)
    {
    // directional lights
        for (int i = numPointLights; i < numLights; ++i)
        {
            Light light = gLights[i];
        
            float3 L_central = normalize(-light.dirType.xyz);
            float3 L_shadow = L_central;
            float dist = 10000.0f;            

            float NdotL = max(dot(normal, L_central), 0.0f);
        
            if (NdotL > 0.0f)
            {
                RayDesc shadowRay;
                shadowRay.Origin = worldPos + normal * bias;
                shadowRay.Direction = L_shadow;
                shadowRay.TMin = 0.01f;
                shadowRay.TMax = dist - 0.05f;

                ShadowPayload shadowPayload;
                shadowPayload.isVisible = false;
            
                TraceRay(gScene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
                     0xFF, 0, 1, 1, shadowRay, shadowPayload);

                if (shadowPayload.isVisible)
                {
                    float3 H = normalize(V + L_central);
                    float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
                    float NDF = DistributionGGX(normal, H, roughness);
                    float G = GeometrySmith(normal, V, L_central, roughness, false);

                    float3 kS = F;
                    float3 kD = (1.0f - kS) * (1.0f - metalness);

                    float3 diffuseFactor = kD / PI;
                    float3 specularFactor = (NDF * G * F) / (4.0f * max(dot(normal, V), 0.0f) * NdotL + 0.001f);

                    directDiffuseIrradiance += diffuseFactor * light.diffuseColor.rgb * NdotL;
                    directSpecular += specularFactor * light.specularColor.rgb * NdotL;
                }
            }
        }

        if (numPointLights > 0)
        {
            float3 accumDiffuse = float3(0, 0, 0);
            float3 accumSpecular = float3(0, 0, 0);
            
            for (int lightPass = 0; lightPass < shadowRays; ++lightPass)
            {
                // --- RIS Sampling for Point Lights ---
        
        // RIS State
                int selectedLightIndex = -1;
                float totalWeight = 0.0f;
                float selectedTargetPdf = 0.0f;

        
        // --- RIS LOOP: Pick the best light ---
            [loop]
                for (int i = 0; i < risCandidates; ++i)
                {
            // Pick a random candidate uniformly
                    int candidateIndex = min(int(nextRand(seed) * float(numPointLights)), numPointLights - 1);
                    Light candidate = gLights[candidateIndex];
            
            // Calculate target PDF (Importance)
                    float weight = EvaluateLightImportance(candidate, worldPos, normal);
            
            // Streaming Reservoir Sampling update
                    totalWeight += weight;
                    if (nextRand(seed) * totalWeight < weight)
                    {
                        selectedLightIndex = candidateIndex;
                        selectedTargetPdf = weight;
                    }
                }
        
        // --- SHADING: Trace shadow ray for the winner ---
                if (selectedLightIndex != -1 && totalWeight > 0.0f)
                {
                    Light light = gLights[selectedLightIndex];
            
            // Calculate Monte Carlo Weight
            
            // Standard RIS Weight formula:
            // W = (1 / TargetPDF_y) * (1/M) * Sum(TargetPDF_xi / SourcePDF_xi)
            // Since SourcePDF is constant (1/N) for all candidates:
            // W = (1 / TargetPDF_y) * (1/M) * (TotalWeight / (1/N))
            // W = (TotalWeight * N) / (M * TargetPDF_y)
            
                    float risWeight = (totalWeight * float(numPointLights)) / (float(risCandidates) * selectedTargetPdf);
            
            // Perform standard lighting calculation
                    float3 toLight = light.position.xyz - worldPos;
                    float dist = length(toLight);
                    float3 L_central = normalize(toLight);
                    float3 L_shadow = GetConeSample(seed, L_central, radians(5.0f));
                    float attenuation = 1.0f / (kConst + kLinear * dist + kQuadratic * dist * dist);
                    float NdotL = max(dot(normal, L_central), 0.0f);

                    if (NdotL > 0.0f && attenuation > 0.001f)
                    {
                        RayDesc shadowRay;
                        shadowRay.Origin = worldPos + normal * bias;
                        shadowRay.Direction = L_shadow;
                        shadowRay.TMin = 0.01f;
                        shadowRay.TMax = dist - 0.05f;

                        ShadowPayload shadowPayload;
                        shadowPayload.isVisible = false;
                
                        TraceRay(gScene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
                         0xFF, 0, 1, 1, shadowRay, shadowPayload);

                        if (shadowPayload.isVisible)
                        {
                    // PBR Math
                            float3 H = normalize(V + L_central);
                            float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
                            float NDF = DistributionGGX(normal, H, roughness);
                            float G = GeometrySmith(normal, V, L_central, roughness, false);

                            float3 kS = F;
                            float3 kD = (1.0f - kS) * (1.0f - metalness);

                            float3 diffuseFactor = kD / PI;
                            float3 specularFactor = (NDF * G * F) / (4.0f * max(dot(normal, V), 0.0f) * NdotL + 0.001f);

                    // Apply RIS Weight
                            accumDiffuse += diffuseFactor * light.diffuseColor.rgb * attenuation * NdotL * risWeight;
                            accumSpecular += specularFactor * light.specularColor.rgb * attenuation * NdotL * risWeight;
                        }
                    }
                }
            }
            directDiffuseIrradiance += accumDiffuse / float(shadowRays);
            directSpecular += accumSpecular / float(shadowRays);
        }    
    }
    else
    {
        for (int i = 0; i < numPointLights; i++)
        {
            Light light = gLights[i];
            float3 toLight = light.position.xyz - worldPos;
            float dist = length(toLight);
            float3 L = normalize(toLight);
            float attenuation = 1.0f / (kConst + kLinear * dist + kQuadratic * dist * dist);
            float NdotL = max(dot(normal, L), 0.0f);
            
            if (NdotL > 0.0f && attenuation > 0.001f)
            {
                // PBR Math
                float3 H = normalize(V + L);
                float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
                float NDF = DistributionGGX(normal, H, roughness);
                float G = GeometrySmith(normal, V, L, roughness, false);

                float3 kS = F;
                float3 kD = (1.0f - kS) * (1.0f - metalness);

                float3 diffuseFactor = kD / PI;
                float3 specularFactor = (NDF * G * F) / (4.0f * max(dot(normal, V), 0.0f) * NdotL + 0.001f);

                    // Apply RIS Weight
                directDiffuseIrradiance += diffuseFactor * light.diffuseColor.rgb * attenuation * NdotL;
                directSpecular += specularFactor * light.specularColor.rgb * attenuation * NdotL;
            }
        }
        
        for (int i = numPointLights; i < numLights; i++)
        {
            Light light = gLights[i];
        
            float3 L = normalize(-light.dirType.xyz);
            float NdotL = max(dot(normal, L), 0.0f);
        
            if (NdotL > 0.0f)
            {
                float3 H = normalize(V + L);
                float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
                float NDF = DistributionGGX(normal, H, roughness);
                float G = GeometrySmith(normal, V, L, roughness, false);
                float3 kS = F;
                float3 kD = (1.0f - kS) * (1.0f - metalness);
                float3 diffuseFactor = kD / PI;
                float3 specularFactor = (NDF * G * F) / (4.0f * max(dot(normal, V), 0.0f) * NdotL + 0.001f);
                directDiffuseIrradiance += diffuseFactor * light.diffuseColor.rgb * NdotL;
                directSpecular += specularFactor * light.specularColor.rgb * NdotL;
            }
        }
    }
    
    float3 reflectedColor = float3(0, 0, 0);
    float3 transmittedColor = float3(0, 0, 0);

    // Fresnel (Schlick)
    float3 F = FresnelSchlick(NdotV, F0);

    // --- Recursive Reflection ---
    //float3 reflectedColor = float3(0, 0, 0);
    [branch]
    if (reflectionsEnabled && payload.reflectionDepth < maxReflectionDepth)
    {
        float2 Xi = float2(nextRand(seed), nextRand(seed));
        float3 H = ImportanceSampleGGX(Xi, normal, roughness);
        float3 R = normalize(reflect(-V, H));
        
        float NdotL = saturate(dot(normal, R));
        float NdotV = saturate(dot(normal, V));
        float NdotH = saturate(dot(normal, H));
        float VdotH = saturate(dot(V, H));
        
        if (dot(normal, R) > 0.0f)
        {
            RayDesc ray;
            ray.Origin = worldPos + normal * bias;
            //ray.Origin = GetShadowRayOrigin(worldPos, normal);
            ray.Direction = R;
            ray.TMin = 0.01f;
            ray.TMax = 1000.0f;
            RayPayload reflPayload;
            reflPayload.color = float3(0, 0, 0);
            reflPayload.reflectionDepth = payload.reflectionDepth + 1;
            reflPayload.transparentDepth = payload.transparentDepth;
            reflPayload.hitT = 0.0f;
            reflPayload.dDdx = reflect(payload.dDdx, normal);
            reflPayload.dDdy = reflect(payload.dDdy, normal);
            reflPayload.blendAlbedo = float4(0, 0, 0, 0);
            reflPayload.blendDiffuse = float3(0, 0, 0);
            reflPayload.blendF0 = float3(0, 0, 0);
            reflPayload.blendMaterial = float2(0, 0);
            reflPayload.minDecalT = 1e20f;
        
            TraceRay(gScene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, ray, reflPayload);
        
            float3 F = FresnelSchlick(VdotH, F0); // Note: Use VdotH for microfacet Fresnel
        
            // Use the IBL version of GeometrySmith for reflections
            float G = GeometrySmith(normal, V, R, roughness, true);
        
            // Weight derived from canceling PDF terms with BRDF terms
            // Weight = F * G * VdotH / (NdotV * NdotH)
            float3 weight = F * G * VdotH / (NdotV * NdotH + 0.00001f);

            reflectedColor += reflPayload.color * weight;
        }
    }
    
    if (transmission > 0.0f && payload.transparentDepth < maxTransparentDepth)
    {
        // Manage IOR Ratio (eta)
        float eta = 1.0f / gIOR; // Air -> Glass
        float3 N_refract = normal;
        
        // If we are hitting a backface, we are exiting the medium
        if (isBackFace)
        {
            eta = gIOR; // Glass -> Air
            //N_refract = -normal; // Flip normal for refraction math
        }

        // Importance Sample the Refracted Ray based on Roughness
        float pdf;
        float2 Xi = float2(nextRand(seed), nextRand(seed));
        
        // Use the helper to get microfacet normal H
        float3 H = ImportanceSampleGGX_Transmission(Xi, N_refract, roughness, gIOR, pdf);
        
        // Calculate Refraction Direction using Snell's Law on the microfacet
        float3 refDir = refract(-V, H, eta);
        
        // Check for Total Internal Reflection (refract returns 0,0,0)
        if (length(refDir) > 0.0f)
        {
            RayDesc transRay;
            transRay.Origin = worldPos - N_refract * bias; // Bias *inwards*
            transRay.Direction = normalize(refDir);
            transRay.TMin = 0.001f;
            transRay.TMax = 1000.0f;

            RayPayload transPayload;
            transPayload.color = float3(0, 0, 0);
            transPayload.reflectionDepth = payload.reflectionDepth;
            transPayload.transparentDepth = payload.transparentDepth + 1;
            transPayload.hitT = 0.0f; // IMPORTANT: Will be filled by next hit
            transPayload.dDdx = payload.dDdx;
            transPayload.dDdy = payload.dDdy;
            transPayload.blendAlbedo = float4(0, 0, 0, 0);
            
            // We usually want double-sided intersection for volume boundaries
            TraceRay(gScene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, transRay, transPayload);
            
            float3 incomingLight = transPayload.color;

            // --- KHR_materials_volume (Beer's Law) ---
            // Attenuation applies when light travels THROUGH the medium.
            // If we are currently inside the mesh (isBackFace is True for entry in some conventions, 
            // but in standard single-sided tracing:
            // 1. Hit Front Face (Enter) -> Trace Refraction
            // 2. Hit Back Face (Exit) -> The distance between 1 and 2 is "volume".
            
            // Ideally, we apply attenuation if the ray WE JUST TRACED traveled through volume.
            // If we just entered (Hit Front Face), the ray travels inside.
            // If we just exited (Hit Back Face), the ray travels outside (air).

            if (!isBackFace) // We are entering, so the ray travels through the object
            {
                // Beer's Law: exp(-sigma * distance)
                // sigma = -log(attenuationColor) / attenuationDistance
                float3 attColor = max(gAttenuationColor.rgb, 0.0001f);
                float attDist = max(gAttenuationDistance, 0.0001f);
                float3 sigma = -log(attColor) / attDist;
                
                // transPayload.hitT is the distance the transmission ray traveled before hitting the backface
                float3 transmittance = exp(-sigma * transPayload.hitT);
                
                incomingLight *= transmittance;
            }

            transmittedColor = incomingLight;
        }
        else
        {
float3 tirDir = reflect(-V, H); // Reflect view off microfacet H
            
            if (length(tirDir) > 0.0f)
            {
                RayDesc tirRay;
                
                // BIAS CALCULATION:
                // We are inside (BackFace). 'N_refract' points IN (towards camera).
                // We want to stay IN. So we push ALONG the normal.
                // (Contrast with Refraction above where we used minus (-) to push OUT).
                tirRay.Origin = worldPos + N_refract * bias; 
                
                tirRay.Direction = normalize(tirDir);
                tirRay.TMin = 0.001f;
                tirRay.TMax = 1000.0f;

                RayPayload transPayload;
                transPayload.color = float3(0, 0, 0);
                transPayload.reflectionDepth = payload.reflectionDepth + 1; // Count as a bounce
                transPayload.transparentDepth = payload.transparentDepth + 1;
                transPayload.hitT = 0.0f;
                transPayload.dDdx = payload.dDdx;
                transPayload.dDdy = payload.dDdy;
                transPayload.blendAlbedo = float4(0, 0, 0, 0);
                
                TraceRay(gScene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, tirRay, transPayload);
                
                float3 tirColor = transPayload.color;

                // --- Apply Volume Attenuation (Beer's Law) ---
                // The TIR ray is traveling through the volume, just like an entering ray.
                // If we are currently inside (isBackFace), this new ray continues inside.
                if (isBackFace)
                {
                    float3 attColor = max(gAttenuationColor.rgb, 0.0001f);
                    float attDist = max(gAttenuationDistance, 0.0001f);
                    float3 sigma = -log(attColor) / attDist;
                    
                    // Attenuate based on how far the TIR ray traveled
                    tirColor *= exp(-sigma * transPayload.hitT);
                }

                // TIR creates a perfect reflection, so we add it to the transmission lobe
                transmittedColor = tirColor;
            }
        }
    }
    
    float3 diffuseLobe = directDiffuseIrradiance * albedo;
    float3 finalDiffuseTrans = lerp(diffuseLobe, transmittedColor * albedo, transmission);
    float3 finalSpecular = directSpecular + reflectedColor;
    // --- Calculate Final Color for this surface ---
    // Combined = (Irradiance * Albedo) + Specular + Emissive + Reflections
    float3 myFinalColor = finalDiffuseTrans + finalSpecular + emissive;
    //float3 myFinalColor = (directDiffuseIrradiance * albedo) + directSpecular + emissive + reflectedColor;

    float4 myAlbedo = float4(albedo, alpha);
    float3 myDiffuse = directDiffuseIrradiance;
    float3 myF0 = F0;
    float2 myMaterial = float2(roughness, metalness);
    // --- Recursive Transparency ---
    if (isTransparent && payload.transparentDepth < maxTransparentDepth && alpha < 1.0f)
    {
        RayDesc transRay;
        transRay.Origin = worldPos + WorldRayDirection() * 0.001f;
        transRay.Direction = WorldRayDirection();
        transRay.TMin = 0.001f;
        transRay.TMax = 1000.0f;

        RayPayload transPayload;
        transPayload.color = float3(0, 0, 0);
        transPayload.transparentDepth = payload.transparentDepth + 1;
        transPayload.reflectionDepth = payload.reflectionDepth;
        transPayload.hitT = 0.0f;
        transPayload.dDdx = payload.dDdx;
        transPayload.dDdy = payload.dDdy;
        transPayload.blendAlbedo = float4(0, 0, 0, 0);
        transPayload.blendDiffuse = float3(0, 0, 0);
        transPayload.blendF0 = float3(0, 0, 0);
        transPayload.blendMaterial = float2(0, 0);
        transPayload.minDecalT = 1e20f;

        TraceRay(gScene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, transRay, transPayload);

        // Blend with background
        myFinalColor = lerp(transPayload.color, myFinalColor, alpha);
        myAlbedo = lerp(transPayload.blendAlbedo, myAlbedo, alpha);
        myDiffuse = lerp(transPayload.blendDiffuse, myDiffuse, alpha);
        myF0 = lerp(transPayload.blendF0, myF0, alpha);        
        myMaterial = lerp(transPayload.blendMaterial, myMaterial, alpha);
    }
    else if (!isTransparent)
    {
        myAlbedo.a = 1.0f;
    }

    // --- Output to Payload (Pass up the stack) ---
    payload.color = myFinalColor;
    payload.hitT = RayTCurrent();
    payload.blendAlbedo = myAlbedo;
    payload.blendF0 = myF0;
    payload.blendDiffuse = myDiffuse;
    payload.blendMaterial = myMaterial;

    // --- Write Split Buffers (Only for Primary Ray) ---
    if (payload.reflectionDepth == 0 && payload.transparentDepth == 0)
    {
        uint2 pixel = DispatchRaysIndex().xy;
        
        // Write Demodulated Diffuse (Irradiance)
        //gOutDiffuse[pixel] = float4(directDiffuseIrradiance, 1.0f);
        //gOutDiffuse[pixel] = float4(myDiffuse, 1.0f);
        
        // Write Specular (Direct + Reflection)
        // Note: Reflections are technically 'Indirect Specular', often stored in same buffer or separate depending on denoiser.
        // For standard composition: Specular = DirectSpec + Reflections
        //gOutSpecular[pixel] = float4(directSpecular + reflectedColor, 1.0f);
        
        //gOutAlbedo[pixel] = float4(albedo, alpha);
        gOutAlbedo[pixel] = myAlbedo;
        gOutAlbedoSpecular[pixel] = float4(myF0, 1.0f);
        
        gOutNormal[pixel] = float4(normal, 0.0f);
        gOutEmissive[pixel] = float4(emissive, 1.0f);
        //    Matches DeferredRT.hlsl packing: x=Roughness, y=Metalness
        //gOutMaterial[pixel] = float2(roughness, metalness);
        gOutMaterial[pixel] = myMaterial;
    }
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    DoShading(payload, attr, false);
}

// Separate entry for Transparent Hit Group (if you use one)
// If you reuse the same ClosestHit, you need to determine transparency via alpha lookup inside DoShading
// or use "AnyHit" to cull opaque hits. For Full RT, usually we handle it in CH.
[shader("closesthit")]
void ClosestHitTransparent(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    DoShading(payload, attr, true);
}


// 4. ANY HIT (Alpha Testing)
[shader("anyhit")]
void AnyHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{    
    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);    
    float2 uv = GetHitUV(PrimitiveIndex(), bary);

    float alpha = gAlbedoMap.SampleLevel(gSampler, uv, 0).a * gBaseColorFactor.a;
    
    if (alpha < gAlphaCutoff)
    {
        IgnoreHit();
    }
}

[shader("anyhit")]
void AnyHitTransparent(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{  
    // --- SHADOW RAYS: Stochastic Transparency ---
    // Only apply stochastic logic if this is a shadow ray (checking the flag used in DoShading)
    if ((RayFlags() & RAY_FLAG_SKIP_CLOSEST_HIT_SHADER))
    {
        // 1. Calculate Barycentrics & UVs
        float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
        float2 uv = GetHitUV(PrimitiveIndex(), bary);
        
        // 2. Sample Alpha
        float alpha = gAlbedoMap.SampleLevel(gSampler, uv, 0).a * gBaseColorFactor.a;
        
        // 3. Stochastic Alpha Test
        uint2 pixel = DispatchRaysIndex().xy;
        uint seed = initRand(pixel.x + frameCount * 17, pixel.y + frameCount * 31);
        
        // If the random number is greater than alpha, let the light pass through (IgnoreHit).
        // Example: Alpha 0.2 (Glass) -> 80% chance to IgnoreHit (Light passes).
        // Example: Alpha 0.9 (Dark Glass) -> 10% chance to IgnoreHit.
        if (nextRand(seed) > alpha)
        {
            IgnoreHit();
        }
    }
}

[shader("anyhit")]
void AnyHitDecal(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{   
    //IgnoreHit();
    //return;
    if ((RayFlags() & RAY_FLAG_SKIP_CLOSEST_HIT_SHADER))
    {
        IgnoreHit();
        return;
    }
    if (payload.blendAlbedo.a >= 1.0f)
    {
        IgnoreHit();
        return;
    }
    
    payload.minDecalT = min(payload.minDecalT, RayTCurrent());
    
    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    float2 uv = GetHitUV(PrimitiveIndex(), bary);

    float4 decalAlbedo = gAlbedoMap.SampleLevel(gSampler, uv, 0) * gBaseColorFactor;
    float decalMetal = gMetalnessMap.SampleLevel(gSampler, uv, 0).b * gMetalnessFactor;
    float decalRough = gMetalnessMap.SampleLevel(gSampler, uv, 0).g * gRoughnessFactor;
    float3 decalF0 = lerp(float3(0.04, 0.04, 0.04), decalAlbedo.rgb, decalMetal);

    float visibilityLeft = 1.0f - payload.blendAlbedo.a;
    float weight = decalAlbedo.a * visibilityLeft;
    
    payload.blendAlbedo.rgb += decalAlbedo.rgb * weight;
    payload.blendAlbedo.a += weight;
    
    payload.blendF0 += decalF0 * weight;
    payload.blendMaterial.x += decalRough * weight; // Roughness
    payload.blendMaterial.y += decalMetal * weight; // Metalness

    IgnoreHit();
}