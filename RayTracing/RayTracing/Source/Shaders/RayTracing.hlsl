// --- GLOBAL RESOURCES (Space 0) ---
// Bound once per frame
RWTexture2D<float4> gOutput : register(u0);
RaytracingAccelerationStructure gScene : register(t0);

#define MAX_LIGHTS 25

struct LightData
{
    float4 position;
    float4 dirType; // .xyz = Direction, .w = Type (1 = Directional, 0 = Point)
    float4 diffuseColor;
    float4 specularColor;
};

cbuffer GlobalCB : register(b0)
{
    float4x4 viewProjInverse;
    float4 cameraPos;
    uint numLights;
    float3 _pad;
    LightData lights[MAX_LIGHTS];
};

//cbuffer CameraData : register(b0)
//{
//    float4x4 viewProjInverse;
//    float4 cameraPos;
//};

// --- LOCAL RESOURCES (Space 1) ---
// Bound per-geometry via the Shader Binding Table (SBT)

// 1. Material Constants (Matches C++ MeshMaterialData)
cbuffer MaterialCB : register(b0, space1)
{
    float4 gBaseColorFactor;
    float  gMetalnessFactor;
    float  gRoughnessFactor;
    float  gAlphaCutoff;
    float  _Pad0;
    float4 gEmissiveFactor;
};

// 2. Mesh Buffers (Bindless Access)
ByteAddressBuffer gVertices : register(t0, space1);
ByteAddressBuffer gIndices  : register(t1, space1);

Texture2D<float4> gAlbedoMap : register(t2, space1);
Texture2D<float4> gMetalnessMap : register(t3, space1);
Texture2D<float4> gRoughnessMap : register(t4, space1);
Texture2D<float4> gNormalMap : register(t5, space1);
Texture2D<float4> gEmissiveMap : register(t6, space1);

SamplerState gSampler : register(s0);

// --- PAYLOADS & ATTRIBUTES ---
struct RayPayload
{
    float4 color;
    float  hitT;
    uint recursionDepth;
};

struct Attributes 
{
    float2 bary;
};

// --- GEOMETRY HELPERS ---
struct Vertex
{
    float3 pos;
    float3 norm;
    float2 uv;
    float4 tan;
};

// --- RANDOM NUMBER GENERATOR ---
// A simple hash function to generate random numbers based on position
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

// Get a random vector inside a cone around the Light Direction (L)
// spreadAngle: The size of the light (in radians). 0 = Point Light, 0.1 = Soft.
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

// --- LIGHTING HELPER ---
float3 CalculateLighting(float3 worldPos, float3 N, float3 albedo, uint2 pixelCoord)
{
    float3 finalColor = float3(0, 0, 0);
    uint seed = initRand(pixelCoord.x, pixelCoord.y);

    for (uint i = 0; i < numLights; ++i)
    {
        LightData light = lights[i];
        
        float3 L_central;
        float lightRadius = 1.0f;
        float attenuation = 1.0f;

        // 1. Calculate Vector to Light (L)
        if (light.dirType.w > 0.5f) // Directional Light
        {
            L_central = normalize(-light.dirType.xyz);
            lightRadius = 0.02f;

        }
        else // Point Light
        {
            float3 lightToPos = light.position.xyz - worldPos;
            float dist = length(lightToPos);
            L_central = normalize(lightToPos);
            
            // Simple Inverse Square Falloff
            attenuation = 1.0f / (1.0f + 0.1f * dist + 0.01f * dist * dist);
            lightRadius = 0.1f;
        }
        
        float NdotL = dot(N, L_central);
        if (NdotL <= 0.0f)
            continue; // Light is behind the surface)
        
        
        float3 L_shadow = L_central;
        GetConeSample(seed, L_central, lightRadius);
        

        // 2. Shadow Ray
        // Offset origin slightly along Normal to prevent self-shadowing (Shadow Acne)
        float3 origin = worldPos + (N * 0.001f);
        
        RayDesc shadowRay;
        shadowRay.Origin = origin;
        shadowRay.Direction = L_shadow;
        shadowRay.TMin = 0.001f;
        
        // If Point light, only trace as far as the light source. 
        // If Directional, trace to infinity (1000.0f).
        float lightDist = (light.dirType.w > 0.5f) ? 1000.0f : distance(light.position.xyz, worldPos);
        shadowRay.TMax = lightDist;

        // Initialize shadow payload
        RayPayload shadowPayload;
        shadowPayload.color = float4(0, 0, 0, 0);
        shadowPayload.hitT = 0.0f; // 0.0 means "Occluded" by default
        shadowPayload.recursionDepth = 0;

        // Trace! 
        // RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH: 
        // Optimization! We don't care about the closest hit, ANY hit means shadow.
        // RAY_FLAG_SKIP_CLOSEST_HIT_SHADER:
        // We don't need to run code on hit, just know that we hit.
        TraceRay(
            gScene,
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_CULL_NON_OPAQUE,
            0xFF, 0, 1, 1, // Miss Shader Index 1 (Create this below!)
            shadowRay,
            shadowPayload
        );

        // 3. Accumulate if not occluded
        if (shadowPayload.hitT < 0.0f) // -1.0 means "Not Occluded"
        {
            float3 diffuse = albedo * light.diffuseColor.rgb * light.diffuseColor.a * NdotL * attenuation;
            finalColor += diffuse;
        }
    }

    return finalColor;
}

// Load 3 indices for the hit triangle
uint3 LoadIndices(uint triangleIndex)
{
    // R32_UINT format = 4 bytes per index
    uint offsetInBytes = triangleIndex * 3 * 4;
    return gIndices.Load3(offsetInBytes);
}

// Load a single vertex from the buffer
Vertex LoadVertex(uint index)
{
    // Your Vertex Stride is 56 bytes:
    // Pos(12) + Norm(12) + UV(8) + Tan(16) + UV(8)
    uint stride = 56;
    uint base = index * stride;

    Vertex v;
    v.pos  = asfloat(gVertices.Load3(base + 0));
    v.norm = asfloat(gVertices.Load3(base + 12));
    v.uv   = asfloat(gVertices.Load2(base + 24));
    v.tan  = asfloat(gVertices.Load4(base + 32));
    return v;
}

// Interpolate vertex attributes using barycentrics
Vertex GetHitSurface(Attributes attr)
{
    uint primitiveID = PrimitiveIndex();
    uint3 indices = LoadIndices(primitiveID);

    Vertex v0 = LoadVertex(indices.x);
    Vertex v1 = LoadVertex(indices.y);
    Vertex v2 = LoadVertex(indices.z);

    float3 bary = float3(1.0 - attr.bary.x - attr.bary.y, attr.bary.x, attr.bary.y);

    Vertex result;
    result.pos  = v0.pos * bary.x + v1.pos * bary.y + v2.pos * bary.z;
    result.norm = normalize(v0.norm * bary.x + v1.norm * bary.y + v2.norm * bary.z);
    result.uv   = v0.uv * bary.x + v1.uv * bary.y + v2.uv * bary.z;
    // Note: Tangent interpolation skipped for brevity, typically not needed for basic shading
    return result;
}

// --- 1. RAY GENERATION ---
[shader("raygeneration")]
void MyRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    float2 uv = (launchIndex + 0.5f) / launchDim;
    float2 screenPos = uv * 2.0f - 1.0f;
    screenPos.y = -screenPos.y;

    // Unproject
    float4 target = mul(viewProjInverse, float4(screenPos, 1.0f, 1.0f));
    float3 rayDir = normalize(target.xyz / target.w - cameraPos.xyz);

    RayDesc ray;
    ray.Origin = cameraPos.xyz;
    ray.Direction = rayDir;
    ray.TMin = 0.001f;
    ray.TMax = 1000.0f;

    RayPayload payload;
    payload.color = float4(0, 0, 0, 0);
    payload.hitT  = -1.0f;
    payload.recursionDepth = 0;

    // Trace
    // U¿ywamy natywnej flagi systemowej
    TraceRay(gScene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, ray, payload);

    gOutput[launchIndex] = payload.color;
}

// --- 2. MISS ---
[shader("miss")]
void MyMiss(inout RayPayload payload)
{
    // Simple Sky
    float3 rayDir = WorldRayDirection();
    float t = 0.5 * (rayDir.y + 1.0);
    payload.color = float4(lerp(float3(1.0, 1.0, 1.0), float3(0.5, 0.7, 1.0), t), 1.0f);
    payload.hitT = -1.0f;
}
[shader("miss")]
void MyShadowMiss(inout RayPayload payload) // Change to RayPayload
{
    payload.hitT = -1.0f; // Use -1.0 to signify "Light Visible"
}

// --- 3. CLOSEST HIT (OPAQUE) ---
// Define a max depth to prevent TDR (GPU Hangs)
static const uint MAX_RECURSION_DEPTH = 6;

[shader("closesthit")]
void MyClosestHit(inout RayPayload payload, in Attributes attr)
{
    Vertex surface = GetHitSurface(attr);
    
    if (HitKind() == HIT_KIND_TRIANGLE_BACK_FACE)
        surface.norm = -surface.norm;
    
    // 1. Sample Texture & Material Data
        float4 albedoSample = gAlbedoMap.SampleLevel(gSampler, surface.uv, 0);
    float3 albedo = albedoSample.rgb * gBaseColorFactor.rgb;
    float alpha = albedoSample.a * gBaseColorFactor.a;

    // 2. Calculate Lighting for the Decal/Surface itself
    float3 L = normalize(float3(0.5, 1.0, -0.5));
    float3 N = surface.norm;
    float NdotL = saturate(dot(N, L));
    // Calculate World Position (Ray Origin + Ray Dir * T)
    float3 worldPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    
    uint2 pixelCoord = DispatchRaysIndex().xy;
    float3 lighting = CalculateLighting(worldPos, surface.norm, albedo, pixelCoord);
    lighting += gEmissiveMap.SampleLevel(gSampler, surface.uv, 0).rgb * gEmissiveFactor.rgb;

    // 3. HANDLE TRANSPARENCY / BLENDING
    float3 finalColor = lighting;

    // If semi-transparent AND we haven't hit our bounce limit...
    if (alpha < 1.0f && payload.recursionDepth < MAX_RECURSION_DEPTH)
    {
        // A. Setup the continuation ray
        RayDesc newRay;
        newRay.Origin = worldPos;
        newRay.Direction = WorldRayDirection(); // Continue straight through
        newRay.TMin = 0.001f; // Offset to avoid self-intersection acne
        newRay.TMax = 1000.0f;

        // B. Create payload for the next bounce
        RayPayload nextPayload;
        nextPayload.color = float4(0, 0, 0, 0);
        nextPayload.hitT = -1.0f;
        nextPayload.recursionDepth = payload.recursionDepth + 1;

        // C. Shoot the ray!
        // This halts the current shader, goes to find the next hit, and returns here.
        TraceRay(gScene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, newRay, nextPayload);

        // D. Blend: (Source * Alpha) + (Dest * (1 - Alpha))
        float3 backgroundColor = nextPayload.color.rgb;
        finalColor = lerp(backgroundColor, lighting, alpha);
    }

    payload.color = float4(finalColor, 1.0f);
    payload.hitT = RayTCurrent();
}

// --- 4. ANY HIT (ALPHA TEST) ---
// Used for Masked Geometry (Leaves, Chain)
[shader("anyhit")]
void MyAnyHit(inout RayPayload payload, in Attributes attr)
{
    // Recalculate barycentrics/UVs just like in ClosestHit
    Vertex surface = GetHitSurface(attr);

    // Sample the Albedo Alpha
    float alpha = gAlbedoMap.SampleLevel(gSampler, surface.uv, 0).a;
    
    // Combine with material factor alpha
    alpha *= gBaseColorFactor.a;

    // If opaque enough, accept hit. If transparent, ignore.
    if (alpha < gAlphaCutoff)
    {
        IgnoreHit();
    }
}