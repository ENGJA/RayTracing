#include "PBR.hlsli"


// --- GLOBAL (Space 0) ---
RaytracingAccelerationStructure gScene : register(t5);
RWTexture2D<float4> gOutColor : register(u0); // FINAL Merged Output (No CompositeCS needed)
RWTexture2D<float4> gOutAlbedo : register(u1);
RWTexture2D<float4> gOutAlbedoSpecular : register(u2);

//RWTexture2D<float4> gOutNormal : register(u2);
//RWTexture2D<float4> gOutAlbedo : register(u3);


// Inputs from G-Buffer
Texture2D<float4> gGBufferAlbedo : register(t0);
Texture2D<float4> gGBufferNormal : register(t1);
Texture2D<float2> gGBufferMaterial : register(t2);
Texture2D<float> gDepth : register(t3);
Texture2D<float4> gEmissive : register(t4);
StructuredBuffer<Light> gLights : register(t6);



cbuffer CBData : register(b0)
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
    
    Light lights[25];
};

// --- LOCAL (Space 1 - From SBT) ---
ByteAddressBuffer gIndices : register(t0, space1);
ByteAddressBuffer gVertices : register(t1, space1);
Texture2D gAlbedoMap : register(t2, space1);
Texture2D gMetalnessMap : register(t3, space1);
Texture2D gRoughnessMap : register(t4, space1);
Texture2D gNormalMap : register(t5, space1);
Texture2D gEmissiveMap : register(t6, space1); 

SamplerState gSampler : register(s0);

struct RayPayload
{
    float4 color;
    uint recursionDepth;
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

VertexAttributes GetVertexAttributes(uint triangleIndex, float2 bary)
{
    uint indexOffset = triangleIndex * 3 * 4;
    uint3 idx = gIndices.Load3(indexOffset);

    // Stride from C++ (Pos=12 + Norm=12 + UV=8 + Tan=16 + UV2=8 = 56)
    const uint stride = 56;
    float3 w = float3(1.0 - bary.x - bary.y, bary.x, bary.y);

    VertexAttributes attr;

    // Normal (Offset 12)
    float3 n0 = asfloat(gVertices.Load3(idx.x * stride + 12));
    float3 n1 = asfloat(gVertices.Load3(idx.y * stride + 12));
    float3 n2 = asfloat(gVertices.Load3(idx.z * stride + 12));
    attr.normal = normalize(n0 * w.x + n1 * w.y + n2 * w.z);

    // UV (Offset 24)
    float2 uv0 = asfloat(gVertices.Load2(idx.x * stride + 24));
    float2 uv1 = asfloat(gVertices.Load2(idx.y * stride + 24));
    float2 uv2 = asfloat(gVertices.Load2(idx.z * stride + 24));
    attr.uv = uv0 * w.x + uv1 * w.y + uv2 * w.z;

    // Tangent (Offset 32)
    float4 t0 = asfloat(gVertices.Load4(idx.x * stride + 32));
    float4 t1 = asfloat(gVertices.Load4(idx.y * stride + 32));
    float4 t2 = asfloat(gVertices.Load4(idx.z * stride + 32));
    attr.tangent = t0 * w.x + t1 * w.y + t2 * w.z;

    return attr;
}

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

// Returns float between 0.0 and 1.0
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


float3 GetWorldPosition(uint2 pixel)
{
    float width, height;
    gOutColor.GetDimensions(width, height);
    float2 uv = (pixel + 0.5) / float2(width, height);
    float z = gDepth.Load(uint3(pixel, 0));

    // Convert to NDC
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    
    float4 clip = float4(ndc, z, 1.0f);
    float4 world = mul(invViewProj, clip);
    return world.xyz / world.w;
}

// --- HELPER: Vertex Fetching ---
// Interpolate UVs using the hit triangle ID and barycentrics
float2 GetHitUV(uint triangleIndex, float2 bary)
{
    // 1. Load Indices (R32_UINT)
    // 3 indices per triangle * 4 bytes per index = 12 bytes stride
    uint indexOffset = triangleIndex * 3 * 4; // 3 indices * 4 bytes
    uint3 idx = gIndices.Load3(indexOffset);

    // 2. Load Vertex UVs
    // Pos(12) + Normal(12) + Tex0(8) + Tangent(16) + MatProps(8) = 56 bytes per vertex
    const uint stride = 56;
    
    // Offset to UV (TEXCOORD0) 
    // Pos(12) + Normal(12) = 24
    const uint uvOffset = 24;
    
    // Safety check: if your stride is different, adjust this!
    float2 uv0 = asfloat(gVertices.Load2(idx.x * stride + uvOffset));
    float2 uv1 = asfloat(gVertices.Load2(idx.y * stride + uvOffset));
    float2 uv2 = asfloat(gVertices.Load2(idx.z * stride + uvOffset));

    // 3. Interpolate
    float3 b = float3(1.0 - bary.x - bary.y, bary.x, bary.y);
    return uv0 * b.x + uv1 * b.y + uv2 * b.z;
}

// Based on "Ray Tracing Gems" Chapter 6
float3 GetShadowRayOrigin(float3 pos, float3 normal)
{
    const float origin = 1.0f / 32.0f;
    const float float_scale = 1.0f / 65536.0f;
    const float int_scale = 256.0f;

    // Offset the position along the normal based on the magnitude of the position components
    int3 of_i = int3(int_scale * normal.x, int_scale * normal.y, int_scale * normal.z);

    float3 p_i = float3(
        asfloat(asint(pos.x) + ((pos.x < 0) ? -of_i.x : of_i.x)),
        asfloat(asint(pos.y) + ((pos.y < 0) ? -of_i.y : of_i.y)),
        asfloat(asint(pos.z) + ((pos.z < 0) ? -of_i.z : of_i.z))
    );

    // Apply a specialized offset relative to the plane equation
    return float3(abs(pos.x) < origin ? pos.x + float_scale * normal.x : p_i.x,
                  abs(pos.y) < origin ? pos.y + float_scale * normal.y : p_i.y,
                  abs(pos.z) < origin ? pos.z + float_scale * normal.z : p_i.z);
}

// --- RAY GEN ---
[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    
    // 1. Check if pixel needs reflection
    float depth = gDepth.Load(uint3(pixel, 0));
    if (depth >= 1.0)
    {
        // Skybox is usually treated as Diffuse or Emissive
        float3 rayDir = normalize(GetWorldPosition(pixel) - viewPos);
        float t = 0.5 * (rayDir.y + 1.0);
        float3 sky = lerp(float3(0.3, 0.3, 0.3), float3(0.5, 0.7, 1.0), t);
        
        //gOutDiffuse[pixel] = float4(1, 1, 1, 1); 
        //gOutSpecular[pixel] = float4(0, 0, 0, 0);
        gOutAlbedo[pixel] = float4(sky, 1.0);
        gOutAlbedoSpecular[pixel] = float4(0, 0, 0, 0);
        //gOutAlbedo[pixel] = float4(0, 0, 0, 0); // Sky has no albedo
        //gOutNormal[pixel] = float4(0, 0, 0, 0);
        return;
    }

    float3 worldPos = GetWorldPosition(pixel);   
    float3 normal = gGBufferNormal.Load(uint3(pixel, 0)).xyz;
    float3 albedo = gGBufferAlbedo.Load(uint3(pixel, 0)).xyz;
    float2 mats = gGBufferMaterial.Load(uint3(pixel, 0));
    float roughness = mats.x;
    float metalness = mats.y;

    float3 V = normalize(viewPos - worldPos);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);    
    
    float3 diffuseTotal = float3(0, 0, 0);
    float3 specularTotal = float3(0, 0, 0);
    
    diffuseTotal += float3(0.05, 0.05, 0.05); // ambient
    
    float distToCamera = length(viewPos - worldPos);
    float bias = 0.001f + distToCamera * 0.01f;
    
    uint seed = initRand(pixel.x + frameCount * 17, pixel.y + frameCount * 31);
    // Direct Lighting
    for (int i = 0; i < numLights; ++i)
    {
        Light light = gLights[i];
        float3 L_central;
        float3 L_shadow;
        float attenuation = 1.0f;
        float dist = 10000.0f;
        
        if (light.dirType.w > 0.5f)
        {
            // Directional Light
            L_central = normalize(-light.dirType.xyz);
            L_shadow = L_central;
        }
        else
        {
            // Point Light
            float3 lightPos = light.position.xyz;
            float3 toLight = lightPos - worldPos;
            dist = length(toLight);
            L_central = normalize(toLight);
            L_shadow = GetConeSample(seed, L_central, radians(5.0f)); // Soft shadows with 5 degree cone
            attenuation = 1.0f / (1.0f + 0.1f * dist + 0.01f * dist * dist);
        }
        
        float NdotL = max(dot(normal, L_central), 0.0f);
        if (NdotL > 0.0f && attenuation > 0.001f)
        {
            bool isVisible = true;
            
            if (shadowsEnabled)
            {
            
                ShadowPayload shadowPayload;
                shadowPayload.isVisible = false;
            
                RayDesc ray;
                ray.Origin = worldPos + normal * bias;
            //ray.Origin = GetShadowRayOrigin(worldPos, normal);
                ray.Direction = L_shadow;
                ray.TMin = 0.01;
                ray.TMax = dist - 0.05f;
            
                static const uint instanceMask = 0x01;
                TraceRay(gScene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
                     instanceMask, 0, 1, 1, ray, shadowPayload);
            
                isVisible = shadowPayload.isVisible;
            }
            
            if (isVisible)
            {
                float3 H = normalize(V + L_central);
                float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
                float NDF = DistributionGGX(normal, H, roughness);
                float G = GeometrySmith(normal, V, L_central, roughness);
                
                // Specular part (kS)
                float3 numerator = NDF * G * F;
                float denominator = 4.0 * max(dot(normal, V), 0.0f) * NdotL + 0.001;
                float3 specular = numerator / denominator;                
                
                // Diffuse part (kD)
                float3 kS = F;
                float3 kD = (1.0 - kS) * (1.0 - metalness);
                float3 diffuse = kD / PI;
                
                float3 diffuseLightRadiance = light.diffuseColor.rgb* attenuation * NdotL;
                float3 specularLightRadiance = light.specularColor.rgb * attenuation * NdotL;
                
                diffuseTotal += diffuse * diffuseLightRadiance;
                specularTotal += specular * specularLightRadiance;
            }
        }                    
    }
    
    // Reflection Trace
    if (reflectionsEnabled)
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
            RayPayload payload;
            payload.color = float4(0, 0, 0, 0);
            payload.recursionDepth = 1;            
        
            TraceRay(gScene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
        
            float3 F = FresnelSchlick(max(dot(normal, V), 0.0f), F0);
            specularTotal += payload.color.rgb * F;
        }
    }
    
    float3 emissive = gEmissive.Load(uint3(pixel, 0)).rgb;
    float3 finalColor = (diffuseTotal * albedo) + specularTotal + emissive;
    gOutColor[pixel] = float4(finalColor, 1.0);
    
    //gOutDiffuse[pixel] = float4(diffuseTotal, 1.0);
    //gOutSpecular[pixel] = float4(specularTotal, 1.0);
    gOutAlbedo[pixel] = float4(albedo, 1.0);
    gOutAlbedoSpecular[pixel] = float4(F0, 1.0);
    //gOutAlbedo[pixel] = float4(albedo, 1.0);
    //gOutNormal[pixel] = float4(normalize(normal) * 0.5 + 0.5, 1.0);    
}

// --- CLOSEST HIT ---
[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // 1. Fetch Geometry & UVs
    uint triangleIndex = PrimitiveIndex();
    VertexAttributes vert = GetVertexAttributes(triangleIndex, attr.barycentrics);

    // 2. Sample LOCAL Textures (Space 1)
    float4 albedoSample = gAlbedoMap.SampleLevel(gSampler, vert.uv, 0);
    float3 albedo = albedoSample.rgb;
    
    // Check packed textures (Standard glTF: Metal=Blue, Rough=Green)
    float metalness = gMetalnessMap.SampleLevel(gSampler, vert.uv, 0).b;
    float roughness = gRoughnessMap.SampleLevel(gSampler, vert.uv, 0).g;

    // 3. Calculate Normal (TBN Basis)
    float3 N = normalize(vert.normal);
    float3 T = normalize(vert.tangent.xyz);
    T = normalize(T - dot(T, N) * N); // Gram-Schmidt re-orthogonalization
    float3 B = cross(N, T) * vert.tangent.w;
    float3x3 TBN = float3x3(T, B, N);

    float3 normalMap = gNormalMap.SampleLevel(gSampler, vert.uv, 0).rgb;
    float3 tangentNormal = normalMap * 2.0 - 1.0;
    float3 worldNormal = normalize(mul(tangentNormal, TBN));

    // 4. Lighting Setup
    float3 hitPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 V = -WorldRayDirection();
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
    float3 Lo = float3(0, 0, 0);

    // 5. Light Loop (Using Global gLights from Space 0)
    for (int i = 0; i < numLights; ++i)
    {
        Light light = gLights[i];
        float3 L;
        float attenuation = 1.0;

        if (light.dirType.w > 0.5f)
        { // Directional
            L = normalize(-light.dirType.xyz);
        }
        else
        { // Point
            float3 toLight = light.position.xyz - hitPos;
            float dist = length(toLight);
            L = normalize(toLight);
            attenuation = 1.0 / (1.0 + 0.1 * dist + 0.01 * dist * dist);
        }

        float NdotL = max(dot(worldNormal, L), 0.0);
        
        if (NdotL > 0.0)
        {
            float3 H = normalize(V + L);
            float NDF = DistributionGGX(worldNormal, H, roughness);
            float G = GeometrySmith(worldNormal, V, L, roughness);
            float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

            float3 kS = F;
            float3 kD = (1.0 - kS) * (1.0 - metalness);

            float3 diffuse = (kD * albedo) / PI;
            float3 specular = (NDF * G * F) / (4.0 * max(dot(worldNormal, V), 0.0) * NdotL + 0.001);

            Lo += (diffuse + specular) * light.diffuseColor.rgb * attenuation * NdotL;
        }
    }
    
    if (payload.recursionDepth < maxReflectionDepth)
    {
        uint seed = initRand(DispatchRaysIndex().x + frameCount * 17, DispatchRaysIndex().y + frameCount * 31);
        
        // Generate reflection direction
        float2 Xi = float2(nextRand(seed), nextRand(seed));
        float3 H = ImportanceSampleGGX(Xi, N, roughness); // Use N from Step 3
        float3 R = normalize(reflect(WorldRayDirection(), H));

        if (dot(N, R) > 0.0f)
        {
            RayDesc ray;
            ray.Origin = hitPos + N * 0.001f; // Bias
            ray.Direction = R;
            ray.TMin = 0.01f;
            ray.TMax = 1000.0f;

            // Create new payload for the next bounce
            RayPayload nextPayload;
            nextPayload.color = float4(0, 0, 0, 0);
            nextPayload.recursionDepth = payload.recursionDepth + 1;

            // RECURSIVE CALL
            TraceRay(gScene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, nextPayload);

            // Add the reflected color to our local result
            // Scaled by Fresnel (F) calculated in your PBR section
            float3 F = FresnelSchlick(max(dot(N, -WorldRayDirection()), 0.0f), F0);
            Lo += nextPayload.color.rgb * F;
        }
    }

    // 6. Add Emissive (Using LOCAL texture, not G-Buffer)
    float3 emissive = gEmissiveMap.SampleLevel(gSampler, vert.uv, 0).rgb;
    
    payload.color = float4(Lo + emissive, 1.0);
}

// --- ANY HIT (Alpha Test) ---
[shader("anyhit")]
void AnyHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint triangleIndex = PrimitiveIndex();
    float2 uv = GetHitUV(triangleIndex, attr.barycentrics); // <--- Fixed

    float alpha = gAlbedoMap.SampleLevel(gSampler, uv, 0).a;
    if (alpha < 0.5f)
        IgnoreHit();
}



// --- MISS SHADER ---
[shader("miss")]
void Miss(inout RayPayload payload)
{
    // Simple Gradient Sky
    float3 rayDir = WorldRayDirection();
    float t = 0.5 * (rayDir.y + 1.0);
    float3 sky = lerp(float3(0.3, 0.3, 0.3), float3(0.5, 0.7, 1.0), t);
    
    payload.color = float4(sky, 1.0);
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
    payload.isVisible = true;
}


[shader("closesthit")]
void ClosestHitTransparent(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    
}

[shader("anyhit")]
void AnyHitTransparent(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    
}