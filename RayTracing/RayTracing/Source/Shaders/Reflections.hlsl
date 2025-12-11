#include "PBR.hlsli"

// --- GLOBAL (Space 0) ---
RaytracingAccelerationStructure gScene : register(t5);
RWTexture2D<float4> gReflectionOutput : register(u0);

// Inputs from G-Buffer
Texture2D<float4> gGBufferNormal : register(t1);
Texture2D<float2> gGBufferMaterial : register(t2);
Texture2D<float> gDepth : register(t3);
Texture2D<float4> gEmissive : register(t4);

cbuffer FrameCB : register(b0)
{
    float4x4 vpMatrix : packoffset(c0);
    float4x4 invViewProj : packoffset(c4); // For reconstructing world position
    
    float3 viewPos : packoffset(c8);
    int numLights : packoffset(c8.w);
    
    int frameCount : packoffset(c9.x);
    float3 _pad : packoffset(c9.y);
};

// --- LOCAL (Space 1 - From SBT) ---
ByteAddressBuffer gIndices : register(t0, space1);
ByteAddressBuffer gVertices : register(t1, space1);
Texture2D gAlbedoMap : register(t2, space1);

SamplerState gSampler : register(s0);

struct RayPayload
{
    float4 color;
};

float3 GetWorldPosition(uint2 pixel)
{
    float width, height;
    gReflectionOutput.GetDimensions(width, height);
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

// --- RAY GEN ---
[shader("raygeneration")]
void RayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    
    // 1. Check if pixel needs reflection
    float depth = gDepth.Load(uint3(launchIndex, 0));
    if (depth >= 1.0)
    {
        gReflectionOutput[launchIndex] = float4(0, 0, 0, 0);
        return; // Sky
    }

    float2 mats = gGBufferMaterial.Load(uint3(launchIndex, 0));
    float metalness = mats.x;
    float roughness = mats.y;

    // Optimization: Don't trace for dull non-metals
    if (metalness < 0.1 && roughness > 0.5)
    {
        gReflectionOutput[launchIndex] = float4(0, 0, 0, 0);
        return;
    }

    // 2. Setup Ray
    float3 normal = gGBufferNormal.Load(uint3(launchIndex, 0)).xyz;
    float3 worldPos = GetWorldPosition(launchIndex);   
    float3 V = normalize(viewPos - worldPos);
    float3 R = reflect(-V, normal);

    RayDesc ray;
    ray.Origin = worldPos + normal * 0.01;
    ray.Direction = R;
    ray.TMin = 0.0;
    ray.TMax = 1000.0;

    RayPayload payload = { float4(0, 0, 0, 0) };
    
    if (any(isnan(ray.Origin)) || any(isnan(ray.Direction)))
        return;
    if (length(ray.Direction) < 0.001)
        return;
    
    // 3. Trace
    TraceRay(gScene, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 1, 0, ray, payload);

    gReflectionOutput[launchIndex] = payload.color;
}

// --- CLOSEST HIT ---
[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // Hardware gives us the Primitive Index (Triangle ID)
    uint triangleIndex = PrimitiveIndex();
    
    // Calculate UVs manually
    float2 uv = GetHitUV(triangleIndex, attr.barycentrics);

    float3 color = gAlbedoMap.SampleLevel(gSampler, uv, 0).rgb;
    payload.color = float4(color, 1.0);
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