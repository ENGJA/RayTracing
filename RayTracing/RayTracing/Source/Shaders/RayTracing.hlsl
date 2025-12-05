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

// Calculate the World Space Normal from the Normal Map
float3 CalculateNormal(float3 N, float4 tangent, float2 uv)
{
    // 1. Sample the Normal Map (Range: 0.0 to 1.0)
    float3 normalSample = gNormalMap.SampleLevel(gSampler, uv, 0).rgb;
    
    // 2. Unpack from [0, 1] to [-1, 1]
    float3 tangentNormal = normalSample * 2.0f - 1.0f;

    // 3. Create the TBN Matrix
    // N = World Geometric Normal (from vertex)
    // T = World Tangent (from vertex)
    // B = World Bitangent (Calculated via cross product)
    
    // Re-orthonormalize T with respect to N (Gram-Schmidt process)
    // This fixes artifacts if the mesh scaling skewed the tangent.
    float3 T = normalize(tangent.xyz - dot(tangent.xyz, N) * N);
    
    // Calculate Bitangent
    // tangent.w stores the "handedness" (reflection) of the UVs, usually -1 or 1.
    float3 B = cross(N, T) * tangent.w;

    float3x3 TBN = float3x3(T, B, N);

    // 4. Transform Normal from Tangent Space to World Space
    float3 worldNormal = mul(tangentNormal, TBN);
    
    return normalize(worldNormal);
}

static const float PI = 3.14159265359f;

// Fresnel Schlick approximation
// F0: Surface reflection at zero incidence (0.04 for dielectrics, Albedo for metals)
float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

// Normal Distribution Function (GGX) - Determines how big/sharp the highlight is
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / max(denom, 0.0000001);
}

// Geometry Function (Schlick-GGX) - Determines self-shadowing (micro-facets)
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}


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
float3 CalculateLighting(float3 worldPos, float3 N, float3 V, float3 albedo, float metallic, float roughness, uint2 pixelCoord)
{
    float3 finalColor = albedo * 0.1f; // Ambient term
    uint seed = initRand(pixelCoord.x, pixelCoord.y);
    
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = lerp(F0, albedo, metallic);

    for (uint i = 0; i < numLights; ++i)
    {
        LightData light = lights[i];
        
        float3 L_central;
        float lightRadius = 1.0f;
        float attenuation = 1.0f;
        float lightDistance = 10000.0f;

        // 1. Calculate Vector to Light (L)
        if (light.dirType.w > 0.5f) // Directional Light
        {
            L_central = normalize(-light.dirType.xyz);
            lightRadius = 0.02f;
            lightDistance = 1000.0f;
        }
        else // Point Light
        {
            float3 lightToPos = light.position.xyz - worldPos;
            float dist = length(lightToPos);
            L_central = normalize(lightToPos);
            
            // Simple Inverse Square Falloff
            attenuation = 1.0f / (1.0f + 0.1f * dist + 0.01f * dist * dist);
            lightRadius = 0.1f;
            lightDistance = dist;
        }
        
        float NdotL = dot(N, L_central);
        if (NdotL <= 0.0f)
            continue; // Light is behind the surface)
        
        
        float3 L_shadow = L_central;
        if (lightRadius > 0.0f)
            L_shadow = GetConeSample(seed, L_central, lightRadius);
        

        // 2. Shadow Ray
        // Offset origin slightly along Normal to prevent self-shadowing (Shadow Acne)
        float3 origin = worldPos + (N * 0.001f);
        
        RayDesc shadowRay;
        shadowRay.Origin = origin;
        shadowRay.Direction = L_shadow;
        shadowRay.TMin = 0.001f;
        shadowRay.TMax = lightDistance;
        
        // If Point light, only trace as far as the light source. 
        // If Directional, trace to infinity (1000.0f).
        //float lightDist = (light.dirType.w > 0.5f) ? 1000.0f : distance(light.position.xyz, worldPos);
        //shadowRay.TMax = lightDist;

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
        if (shadowPayload.hitT < 0.0f) // Not Occluded
        {
            float3 L = L_central;
            float3 H = normalize(V + L); // Half vector
            float3 radiance = light.diffuseColor.rgb * light.diffuseColor.a * attenuation * 5.0f; // Boost intensity slightly for PBR

            // Cook-Torrance BRDF
            float NDF = DistributionGGX(N, H, roughness);
            float G = GeometrySmith(N, V, L, roughness);
            float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
           
            float3 numerator = NDF * G * F;
            float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001; // + 0.0001 to prevent divide by zero
            float3 specular = numerator / denominator;
            
            // Energy conservation: diffuse + specular <= 1.0
            float3 kS = F;
            float3 kD = float3(1.0, 1.0, 1.0) - kS;
            kD *= (1.0 - metallic); // Metals have no diffuse

            float NdotL = max(dot(N, L), 0.0);

            // Add to final
            finalColor += (kD * albedo / PI + specular) * radiance * NdotL;
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
    result.tan.xyz = normalize(v0.tan.xyz * bary.x + v1.tan.xyz * bary.y + v2.tan.xyz * bary.z);
    result.tan.w = v0.tan.w;
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
static const uint MAX_RECURSION_DEPTH = 3;

//[shader("closesthit")]
void DoShading(inout RayPayload payload, in Attributes attr, bool isTransparent)
{
    Vertex surface = GetHitSurface(attr);
    
    if (HitKind() == HIT_KIND_TRIANGLE_BACK_FACE)
        surface.norm = -surface.norm;

    // 1. Sample Materials
    float4 albedoSample = gAlbedoMap.SampleLevel(gSampler, surface.uv, 0);
    float3 albedo = albedoSample.rgb * gBaseColorFactor.rgb;
    float alpha = isTransparent ? (albedoSample.a * gBaseColorFactor.a) : 1.0f;
    
    float metalness = gMetalnessMap.SampleLevel(gSampler, surface.uv, 0).b * gMetalnessFactor;
    float roughness = gRoughnessMap.SampleLevel(gSampler, surface.uv, 0).g * gRoughnessFactor;

    // Normal Mapping (Optional - simplified for now, assuming mesh normal)
    //float3 N = normalize(surface.norm);
    float3 N = CalculateNormal(normalize(surface.norm), surface.tan, surface.uv);

    // View Vector (Camera to Surface)
    float3 worldPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 V = normalize(WorldRayOrigin() - worldPos);

    // 2. Direct Lighting (Sun / Lights)
    uint2 pixelCoord = DispatchRaysIndex().xy;
    // Note: Passed V, Metalness and Roughness to the new function
    float3 directLight = CalculateLighting(worldPos, N, V, albedo, metalness, roughness, pixelCoord);
    
    float3 emissive = gEmissiveMap.SampleLevel(gSampler, surface.uv, 0).rgb * gEmissiveFactor.rgb;

    float3 finalColor = directLight + emissive;

    // -------------------------------------------------------------
    // 3. REFLECTION (MIRROR) LOGIC
    // -------------------------------------------------------------
    // Only reflect if we haven't hit max depth and the surface has some reflectivity
    // For PBR, everything reflects, but we can optimize high roughness away.
    if (payload.recursionDepth < MAX_RECURSION_DEPTH)
    {
        // A. Calculate F0 (Reflectivity at 0 degrees)
        float3 F0 = float3(0.04, 0.04, 0.04);
        F0 = lerp(F0, albedo, metalness);

        // B. Calculate Fresnel (How much light reflects vs refracts/absorbs)
        float3 F = fresnelSchlick(max(dot(N, V), 0.0), F0);

        // C. Generate Reflection Ray
        // For pure mirror: reflect(-V, N). 
        float3 R = reflect(-V, N);

        RayDesc reflRay;
        reflRay.Origin = worldPos + (N * 0.001f); // Offset to avoid acne
        reflRay.Direction = R;
        reflRay.TMin = 0.001f;
        reflRay.TMax = 1000.0f;

        RayPayload reflPayload;
        reflPayload.color = float4(0, 0, 0, 0);
        reflPayload.hitT = -1.0f;
        reflPayload.recursionDepth = payload.recursionDepth + 1;

        // D. Trace!
        TraceRay(gScene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, reflRay, reflPayload);

        // E. Composite Reflection
        // For metals: Reflection is tinted by Albedo (handled by F0 interpolation)
        // For dielectrics: Reflection is white (F0=0.04)
        // We multiply by F (Fresnel) because reflections are stronger at glancing angles.
        finalColor += reflPayload.color.rgb * F;
    }

    // -------------------------------------------------------------
    // 4. TRANSPARENCY LOGIC (Glass / Alpha Blending)
    // -------------------------------------------------------------
    if (alpha < 1.0f && payload.recursionDepth < MAX_RECURSION_DEPTH)
    {
        RayDesc transRay;
        transRay.Origin = worldPos + (WorldRayDirection() * 0.001f); // Push forward through surface
        transRay.Direction = WorldRayDirection();
        transRay.TMin = 0.001f;
        transRay.TMax = 1000.0f;

        RayPayload transPayload;
        transPayload.color = float4(0, 0, 0, 0);
        transPayload.hitT = -1.0f;
        transPayload.recursionDepth = payload.recursionDepth + 1;

        TraceRay(gScene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, transRay, transPayload);

        // Simple Blend:
        // Reflective surface color + Transmitted background color
        finalColor = lerp(transPayload.color.rgb, finalColor, alpha);
    }

    payload.color = float4(finalColor, 1.0f);
    payload.hitT = RayTCurrent();
}

// Entry Point 1: For Opaque and Masked Geometry
// Masked geometry handles holes in AnyHit; the remaining surface is opaque.
[shader("closesthit")]
void MyClosestHitOpaque(inout RayPayload payload, in Attributes attr)
{
    DoShading(payload, attr, false); // false = Disable blending
}

// Entry Point 2: For Transparent Geometry
[shader("closesthit")]
void MyClosestHitTransparent(inout RayPayload payload, in Attributes attr)
{
    DoShading(payload, attr, true); // true = Enable blending
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
