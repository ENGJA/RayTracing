#include "PBR.hlsli"

// ===============================================================================================
// --- GLOBAL RESOURCES (Space 0) ---
// ===============================================================================================

// Output UAVs (Matches DeferredRT for DLSS)
RWTexture2D<float4> gOutDiffuse : register(u0); // Diffuse Radiance
RWTexture2D<float4> gOutSpecular : register(u1); // Specular Radiance
RWTexture2D<float4> gOutAlbedo : register(u2); // Base Color (Albedo)
RWTexture2D<float4> gOutAlbedoSpecular : register(u3); // Specular Albedo (F0)
RWTexture2D<float4> gOutColor : register(u4); // FINAL Merged Output (No CompositeCS needed)

RWTexture2D<float4> gOutNormal : register(u5); // World Space Normals
RWTexture2D<float4> gOutEmissive : register(u6); // Emissive
RWTexture2D<float2> gOutMaterial : register(u7); // Roughness/Metalness (matches G-Buffer format)

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
    float2 _pad;
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
    float _Pad0;
    float4 gEmissiveFactor;
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
    uint recursionDepth;
    float hitT; // -1.0 if miss
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
float3 CalculateNormal(float3 N, float4 tangent, float2 uv)
{
    float3 normalSample = gNormalMap.SampleLevel(gSampler, uv, 0).rgb;
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
    float att = 1.0f / (1.0f + 0.1f * dist + 0.01f * distSq);
    
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

    // Unproject to World
    float4 target = mul(invViewProj, float4(ndc, 1.0f, 1.0f));
    float3 rayDir = normalize(target.xyz / target.w - viewPos);

    RayDesc ray;
    ray.Origin = viewPos;
    ray.Direction = rayDir;
    ray.TMin = 0.001f;
    ray.TMax = 10000.0f;

    RayPayload payload;
    payload.color = float3(0, 0, 0);
    payload.recursionDepth = 0;
    payload.hitT = 0.0f;

    // Trace Primary Ray
    TraceRay(gScene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, ray, payload);

    gOutColor[pixel] = float4(payload.color, 1.0f);
    
    // Note: Albedo and F0 are written inside ClosestHit for the primary ray.
    // If it was a miss, we clear them here to be safe.
    if (payload.hitT < 0.0f)
    {
        gOutDiffuse[pixel] = float4(0, 0, 0, 0);
        gOutSpecular[pixel] = float4(0, 0, 0, 0);
        gOutAlbedo[pixel] = float4(0, 0, 0, 0);
        gOutAlbedoSpecular[pixel] = float4(0, 0, 0, 0);
        gOutNormal[pixel] = float4(0, 0, 0, 0);
        gOutEmissive[pixel] = float4(0, 0, 0, 0);
        gOutMaterial[pixel] = float2(0, 0);
    }
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
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
    payload.isVisible = true;
}

// 3. CLOSEST HIT
static const uint MAX_RECURSION_DEPTH = 2; // Keep low for performance

void DoShading(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr, bool isTransparent)
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint triangleIndex = PrimitiveIndex();
    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    VertexAttributes vert = GetHitSurface(triangleIndex, bary);

    // --- Material Sampling ---
    float4 albedoSample = gAlbedoMap.SampleLevel(gSampler, vert.uv, 0);
    float3 albedo = albedoSample.rgb * gBaseColorFactor.rgb;
    float alpha = albedoSample.a * gBaseColorFactor.a;
    float metalness = gMetalnessMap.SampleLevel(gSampler, vert.uv, 0).b * gMetalnessFactor;
    float roughness = gMetalnessMap.SampleLevel(gSampler, vert.uv, 0).g * gRoughnessFactor;
    float3 emissive = gEmissiveMap.SampleLevel(gSampler, vert.uv, 0).rgb * gEmissiveFactor.rgb;
    float3 normal = CalculateNormal(normalize(vert.normal), vert.tangent, vert.uv);
    
    float3 worldPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 V = -WorldRayDirection();
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);

    // --- Direct Lighting ---
    float3 directDiffuseIrradiance = float3(0, 0, 0); // Pure Light (No Albedo)
    float3 directSpecular = float3(0, 0, 0);
    
    float distToCamera = length(viewPos - worldPos);
    float bias = 0.001f + distToCamera * 0.002f;
    uint seed = initRand(pixel.x + frameCount * 17, pixel.y + frameCount * 31);
    
    // directional lights
    for (int i = numPointLights; i < numLights; ++i)
    {
        Light light = gLights[i];
        
        float3 L_central = normalize(-light.dirType.xyz);
        float3 L_shadow = L_central;
        float dist = 10000.0f;
        float attenuation = 1.0f;

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
            
            TraceRay(gScene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
                     0xFF, 0, 1, 1, shadowRay, shadowPayload);

            if (shadowPayload.isVisible)
            {
                float3 H = normalize(V + L_central);
                float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
                float NDF = DistributionGGX(normal, H, roughness);
                float G = GeometrySmith(normal, V, L_central, roughness);

                float3 kS = F;
                float3 kD = (1.0f - kS) * (1.0f - metalness);

                float3 diffuseFactor = kD / PI;
                float3 specularFactor = (NDF * G * F) / (4.0f * max(dot(normal, V), 0.0f) * NdotL + 0.001f);

                directDiffuseIrradiance += diffuseFactor * light.diffuseColor.rgb * attenuation * NdotL;
                directSpecular += specularFactor * light.specularColor.rgb * attenuation * NdotL;
            }
        }
    }

    if (numPointLights > 0)
    {
        // CONSTANTS
        const int M = 8; // Number of candidates to check (higher = stable, lower = fast)
        
        // RIS State
        int selectedLightIndex = -1;
        float totalWeight = 0.0f;
        float selectedTargetPdf = 0.0f;
        float samplePdf = 1.0f / float(numPointLights); // Uniform source PDF (1/N)
        
        // --- RIS LOOP: Pick the best light ---
        for (int i = 0; i < M; ++i)
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
            // W = (1/M) * (TotalWeight / TargetPdf)
            // Note: In RIS, the weight simplifies to (TotalWeight / (M * TargetPdf)) * SourcePdf?
            // Actually for simple RIS where source is uniform 1/N:
            // Weight = (TotalWeight / M) * (1 / TargetPdf) is WRONG for 1-sample.
            
            // Correct Estimator for 1-sample RIS from uniform source:
            // Contribution = LightShader * (TotalImportance / (M * SourcePdf)) ? No.
            
            // Standard RIS Weight formula:
            // W = (1 / TargetPDF_y) * (1/M) * Sum(TargetPDF_xi / SourcePDF_xi)
            // Since SourcePDF is constant (1/N) for all candidates:
            // W = (1 / TargetPDF_y) * (1/M) * (TotalWeight / (1/N))
            // W = (TotalWeight * N) / (M * TargetPDF_y)
            
            float risWeight = (totalWeight * float(numPointLights)) / (float(M) * selectedTargetPdf);
            
            // Perform standard lighting calculation
            float3 toLight = light.position.xyz - worldPos;
            float dist = length(toLight);
            float3 L_central = normalize(toLight);
            float3 L_shadow = GetConeSample(seed, L_central, radians(5.0f));
            float attenuation = 1.0f / (1.0f + 0.1f * dist + 0.01f * dist * dist);
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
                
                TraceRay(gScene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE,
                         0xFF, 0, 1, 1, shadowRay, shadowPayload);

                if (shadowPayload.isVisible)
                {
                    // PBR Math
                    float3 H = normalize(V + L_central);
                    float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
                    float NDF = DistributionGGX(normal, H, roughness);
                    float G = GeometrySmith(normal, V, L_central, roughness);

                    float3 kS = F;
                    float3 kD = (1.0f - kS) * (1.0f - metalness);

                    float3 diffuseFactor = kD / PI;
                    float3 specularFactor = (NDF * G * F) / (4.0f * max(dot(normal, V), 0.0f) * NdotL + 0.001f);

                    // Apply RIS Weight
                    directDiffuseIrradiance += diffuseFactor * light.diffuseColor.rgb * attenuation * NdotL * risWeight;
                    directSpecular += specularFactor * light.specularColor.rgb * attenuation * NdotL * risWeight;
                }
            }
        }
    }
    
    // One random point light
    //if (numPointLights > 0)
    //{

    //    // Pick a random index between [0 ... numPointLights - 1]
    //    int lightIndex = min(int(nextRand(seed) * float(numPointLights)), numPointLights - 1);
        
    //    Light light = gLights[lightIndex];
        
    //    // IMPORTANT: We must multiply the result by numPointLights.
    //    // If we only sample 1 out of N lights, the image will be 1/Nth brightness.
    //    // Multiplying by N compensates for the lights we skipped (Monte Carlo integration).
    //    float stochasticWeight = float(numPointLights);

    //    float3 toLight = light.position.xyz - worldPos;
    //    float dist = length(toLight);
    //    float3 L_central = normalize(toLight);
    //    float3 L_shadow = GetConeSample(seed, L_central, radians(5.0f));
    //    float attenuation = 1.0f / (1.0f + 0.1f * dist + 0.01f * dist * dist);

    //    float NdotL = max(dot(normal, L_central), 0.0f);

    //    if (NdotL > 0.0f && attenuation > 0.001f)
    //    {
    //        RayDesc shadowRay;
    //        shadowRay.Origin = worldPos + normal * bias;
    //        shadowRay.Direction = L_shadow;
    //        shadowRay.TMin = 0.01f;
    //        shadowRay.TMax = dist - 0.05f;

    //        ShadowPayload shadowPayload;
    //        shadowPayload.isVisible = false;
            
    //        TraceRay(gScene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE,
    //                 0xFF, 0, 1, 1, shadowRay, shadowPayload);

    //        if (shadowPayload.isVisible)
    //        {
    //            float3 H = normalize(V + L_central);
    //            float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
    //            float NDF = DistributionGGX(normal, H, roughness);
    //            float G = GeometrySmith(normal, V, L_central, roughness);

    //            float3 kS = F;
    //            float3 kD = (1.0f - kS) * (1.0f - metalness);

    //            float3 diffuseFactor = kD / PI;
    //            float3 specularFactor = (NDF * G * F) / (4.0f * max(dot(normal, V), 0.0f) * NdotL + 0.001f);

    //            // Apply stochastic weight here
    //            directDiffuseIrradiance += diffuseFactor * light.diffuseColor.rgb * attenuation * NdotL * stochasticWeight;
    //            directSpecular += specularFactor * light.specularColor.rgb * attenuation * NdotL * stochasticWeight;
    //        }
    //    }
    //}
    
    //for (int i = 0; i < numLights; ++i)
    //{
    //    Light light = gLights[i];
    //    float3 L_central;
    //    float3 L_shadow;
    //    float dist = 10000.0f;
    //    float attenuation = 1.0f;

    //    if (light.dirType.w > 0.5f) // Directional
    //    {
    //        L_central = normalize(-light.dirType.xyz);
    //        L_shadow = L_central;
    //    }
    //    else // Point
    //    {
    //        float3 toLight = light.position.xyz - worldPos;
    //        dist = length(toLight);
    //        L_central = normalize(toLight);
    //        L_shadow = GetConeSample(seed, L_central, radians(5.0f)); // Soft shadows with 5 degree cone
    //        attenuation = 1.0f / (1.0f + 0.1f * dist + 0.01f * dist * dist);
    //    }

    //    float NdotL = max(dot(normal, L_central), 0.0f);
    //    if (NdotL > 0.0f && attenuation > 0.001f)
    //    {
    //        RayDesc shadowRay;
    //        shadowRay.Origin = worldPos + normal * bias;
    //        shadowRay.Direction = L_shadow;
    //        shadowRay.TMin = 0.01f;
    //        shadowRay.TMax = dist - 0.05f;

    //        ShadowPayload shadowPayload;
    //        shadowPayload.isVisible = false;
            
    //        // Miss Shader 1 = ShadowMiss
    //        TraceRay(gScene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE,
    //                 0xFF, 0, 1, 1, shadowRay, shadowPayload);

    //        if (shadowPayload.isVisible)
    //        {
    //            float3 H = normalize(V + L_central);
    //            float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
    //            float NDF = DistributionGGX(normal, H, roughness);
    //            float G = GeometrySmith(normal, V, L_central, roughness);

    //            float3 kS = F;
    //            float3 kD = (1.0f - kS) * (1.0f - metalness);

    //            // --- KEY CHANGE 1: Diffuse Irradiance only (No Albedo mult) ---
    //            float3 diffuseFactor = kD / PI;
    //            float3 specularFactor = (NDF * G * F) / (4.0f * max(dot(normal, V), 0.0f) * NdotL + 0.001f);

    //            directDiffuseIrradiance += diffuseFactor * light.diffuseColor.rgb * attenuation * NdotL;
    //            directSpecular += specularFactor * light.specularColor.rgb * attenuation * NdotL;
    //        }
    //    }
    //}

    // --- Recursive Reflection ---
    float3 reflectedColor = float3(0, 0, 0);
    if (payload.recursionDepth < MAX_RECURSION_DEPTH)
    {
        float2 Xi = float2(nextRand(seed), nextRand(seed));
        float3 H = ImportanceSampleGGX(Xi, normal, roughness);
        float3 R = normalize(reflect(-V, H));
        
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
            reflPayload.recursionDepth = payload.recursionDepth + 1;
            reflPayload.hitT = 0.0f;
        
            TraceRay(gScene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, ray, reflPayload);
        
            float3 F = FresnelSchlick(max(dot(normal, V), 0.0f), F0);
            reflectedColor += reflPayload.color * F;
        }
    }

    // --- Calculate Final Color for this surface ---
    // Combined = (Irradiance * Albedo) + Specular + Emissive + Reflections
    float3 myFinalColor = (directDiffuseIrradiance * albedo) + directSpecular + emissive + reflectedColor;

    // --- Recursive Transparency ---
    if (isTransparent && payload.recursionDepth < MAX_RECURSION_DEPTH)
    {
        RayDesc transRay;
        transRay.Origin = worldPos + WorldRayDirection() * 0.001f;
        transRay.Direction = WorldRayDirection();
        transRay.TMin = 0.001f;
        transRay.TMax = 1000.0f;

        RayPayload transPayload;
        transPayload.color = float3(0, 0, 0);
        transPayload.recursionDepth = payload.recursionDepth + 1;
        transPayload.hitT = 0.0f;

        TraceRay(gScene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, transRay, transPayload);

        // Blend with background
        myFinalColor = lerp(transPayload.color, myFinalColor, alpha);
    }

    // --- Output to Payload (Pass up the stack) ---
    payload.color = myFinalColor;
    payload.hitT = RayTCurrent();

    // --- Write Split Buffers (Only for Primary Ray) ---
    if (payload.recursionDepth == 0)
    {
        uint2 pixel = DispatchRaysIndex().xy;
        
        // Write Demodulated Diffuse (Irradiance)
        gOutDiffuse[pixel] = float4(directDiffuseIrradiance, 1.0f);
        
        // Write Specular (Direct + Reflection)
        // Note: Reflections are technically 'Indirect Specular', often stored in same buffer or separate depending on denoiser.
        // For standard composition: Specular = DirectSpec + Reflections
        gOutSpecular[pixel] = float4(directSpecular + reflectedColor, 1.0f);
        
        gOutAlbedo[pixel] = float4(albedo, alpha);
        gOutAlbedoSpecular[pixel] = float4(F0, 1.0f);
        
        gOutNormal[pixel] = float4(normal, 0.0f);
        gOutEmissive[pixel] = float4(emissive, 1.0f);
        //    Matches DeferredRT.hlsl packing: x=Roughness, y=Metalness
        gOutMaterial[pixel] = float2(roughness, metalness);
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
    uint triangleIndex = PrimitiveIndex();
    
    // Calculate UV to sample alpha
    // Note: This duplicates fetch logic, but AnyHit needs to be self-contained or use helper
    uint indexOffset = triangleIndex * 3 * 4;
    uint3 idx = gIndices.Load3(indexOffset);
    const uint stride = 56;
    
    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    
    float2 uv0 = asfloat(gVertices.Load2(idx.x * stride + 24));
    float2 uv1 = asfloat(gVertices.Load2(idx.y * stride + 24));
    float2 uv2 = asfloat(gVertices.Load2(idx.z * stride + 24));
    float2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;

    float alpha = gAlbedoMap.SampleLevel(gSampler, uv, 0).a * gBaseColorFactor.a;
    
    if (alpha < gAlphaCutoff)
    {
        IgnoreHit();
    }
}

[shader("anyhit")]
void AnyHitTransparent(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    return;
    // --- 1. Calculate Alpha (Standard Logic) ---
    uint triangleIndex = PrimitiveIndex();
    uint indexOffset = triangleIndex * 3 * 4;
    uint3 idx = gIndices.Load3(indexOffset);
    const uint stride = 56;
    
    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    float2 uv0 = asfloat(gVertices.Load2(idx.x * stride + 24));
    float2 uv1 = asfloat(gVertices.Load2(idx.y * stride + 24));
    float2 uv2 = asfloat(gVertices.Load2(idx.z * stride + 24));
    float2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
    
    // Sample alpha
    float alpha = gAlbedoMap.SampleLevel(gSampler, uv, 0).a * gBaseColorFactor.a;

    // --- 2. SHADOW RAYS: Stochastic Transparency ---
    // Only apply stochastic logic if this is a shadow ray (checking the flag used in DoShading)
    if ((RayFlags() & RAY_FLAG_SKIP_CLOSEST_HIT_SHADER))
    {
        uint2 pixel = DispatchRaysIndex().xy;
        // Generate seed based on pixel and frame to get noise
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