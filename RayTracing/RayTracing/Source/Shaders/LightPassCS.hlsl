#include "PBR.hlsli"

struct Light
{
    float4 position;
    float4 dirType; // .xyz = direction (direction of rays), .w = type flag (1 = directional)
    //float4 color;    // .xyz = color, .w = intensity
    float4 diffuseColor;
    float4 specularColor;
};


// --- Resources ---
// 1. The G-Buffer (Inputs from Raster pass)
Texture2D<float4> gAlbedo : register(t0);
Texture2D<float3> gNormal : register(t1);
Texture2D<float2> gMaterial : register(t2); // Metal/Rough
Texture2D<float> gDepth : register(t3);
Texture2D<float4> gEmissive : register(t4);

// 2. The Scene Structure (For Inline Ray Tracing)
RaytracingAccelerationStructure gTLAS : register(t5);

// 3. Many Lights Data (Structured Buffer instead of Constant Array)
StructuredBuffer<Light> gLights : register(t6);

// 4. Output (The final image or accumulation buffer)
RWTexture2D<float4> gOutput : register(u0);

cbuffer FrameCB : register(b0)
{
    float4x4 vpMatrix : packoffset(c0);
    float4x4 invViewProj : packoffset(c4); // For reconstructing world position
    
    float3 viewPos : packoffset(c8);
    int numLights : packoffset(c8.w);
    
    int frameCount : packoffset(c9.x);
    float3 _pad : packoffset(c9.y);
};

// --- RANDOM HELPERS ---
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

float3 GetConeSample(inout uint seed, float3 L_central, float spreadAngle)
{
    float r1 = nextRand(seed);
    float r2 = nextRand(seed);
    float z = 1.0f - r2 * (1.0f - cos(spreadAngle));
    float phi = 6.2831853f * r1;
    float x = cos(phi) * sqrt(1.0f - z * z);
    float y = sin(phi) * sqrt(1.0f - z * z);
    float3 d = float3(x, y, z);

    float3 up = abs(L_central.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, L_central));
    float3 bitangent = cross(L_central, tangent);
    
    return d.x * tangent + d.y * bitangent + d.z * L_central;
}

// Reconstruct World Position from Depth
float3 GetWorldPosition(float2 uv, float depth)
{
    float4 clipSpace = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipSpace.y = -clipSpace.y; // Flip Y for DX coordinates
    float4 worldPos = mul(invViewProj, clipSpace);
    return worldPos.xyz / worldPos.w;
}

// --- Add this helper function ---
float3 GetSkyColor(float3 direction)
{
    // Simple Gradient Sky (Blue-ish) based on Up vector
    float t = 0.5 * (direction.y + 1.0);
    return lerp(float3(0.3, 0.3, 0.3), float3(0.5, 0.7, 1.0), t);
    
    // OR: Sample a TextureCube if you bind one:
    // return gSkybox.SampleLevel(gSampler, direction, 0).rgb;
}

//static const float PI = 3.14159265359f;

//float DistributionGGX(float3 N, float3 H, float roughness)
//{
//    float a = roughness * roughness;
//    float a2 = a * a;
//    float NdotH = max(dot(N, H), 0.0);
//    float NdotH2 = NdotH * NdotH;
//    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
//    return a2 / (PI * denom * denom + 0.0000001);
//}

//float GeometrySchlickGGX(float NdotV, float roughness)
//{
//    float r = (roughness + 1.0);
//    float k = (r * r) / 8.0;
//    return NdotV / (NdotV * (1.0 - k) + k);
//}

//float GeometrySmith(float3 N, float3 V, float3 L_central, float roughness)
//{
//    float NdotV = max(dot(N, V), 0.0);
//    float NdotL = max(dot(N, L_central), 0.0);
//    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
//}

//float3 FresnelSchlick(float cosTheta, float3 F0)
//{
//    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
//}

// LightPassCS.hlsl - Debug Step 1
//[numthreads(8, 8, 1)]
//void main(uint3 dispatchThreadId : SV_DispatchThreadID)
//{
//    uint2 pixel = dispatchThreadId.xy;
//    float width, height;
//    gOutput.GetDimensions(width, height);
//    if (pixel.x >= width || pixel.y >= height)
//        return;

//    gOutput[pixel] = gAlbedo.Load(uint3(pixel, 0));
//}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    float width, height;
    gOutput.GetDimensions(width, height);
    
    if (pixel.x >= width || pixel.y >= height)
        return;

    // 1. Read Depth. If 1.0 (far plane), draw sky and exit.
    float depth = gDepth.Load(uint3(pixel, 0));
    if (depth >= 1.0f)
    {
        gOutput[pixel] = float4(0.56f, 0.83f, 1.0f, 1.0f); // Simple dark grey background
        return;
    }

    // 2. Decode G-Buffer
    float2 uv = (float2(pixel) + 0.5f) / float2(width, height);
    float3 worldPos = GetWorldPosition(uv, depth);
    float3 normal = normalize(gNormal.Load(uint3(pixel, 0)).xyz);
    float4 albedoData = gAlbedo.Load(uint3(pixel, 0));
    float3 albedo = albedoData.rgb;
    float2 matProps = gMaterial.Load(uint3(pixel, 0));
    float metalness = matProps.x;
    float roughness = matProps.y;

    float3 V = normalize(viewPos - worldPos);
    float3 N = normal;
    float3 finalColor = float3(0, 0, 0);

    // Ambient approximation
    finalColor += albedo * 0.05f;

    // 3. Lighting Loop (Inline Ray Tracing)
    // In a real app, use ReSTIR or tiled culling here. 
    uint seed = initRand(pixel.x + pixel.y * width, frameCount);

    for (uint i = 0; i < numLights; ++i)
    {
        Light light = gLights[i];
        float3 L_central;
        float dist;
        float attenuation = 1.0f;
        float spreadAngle = 0.0f;

        if (light.dirType.w > 0.5f) // Directional
        {
            L_central = normalize(-light.dirType.xyz);
            dist = 10000.0f; // Infinite
            spreadAngle = 0.02f; // ~1 degree soft edge (sun)
        }
        else // Point
        {
            float3 diff = light.position.xyz - worldPos;
            dist = length(diff);
            L_central = normalize(diff);
            
            // Simple Quadratic falloff
            attenuation = 1.0f / (1.0f + 0.1f * dist + 0.05f * dist * dist);
            
            float lightRadius = 1.0f; // Could be a property of the light
            spreadAngle = atan(lightRadius / max(dist, 0.01f));
        }

        float NdotL = max(dot(normal, L_central), 0.0f);
        if (NdotL > 0.0f && attenuation > 0.001f)
        {
            float3 L_shadow = GetConeSample(seed, L_central, spreadAngle);
            
            // --- INLINE RAY TRACING SHADOWS ---
            RayQuery < 
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
            RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
            RAY_FLAG_CULL_BACK_FACING_TRIANGLES
            > q;
            RayDesc ray;
            ray.Origin = worldPos + normal * 0.05f; // Bias to prevent self-shadowing acne
            ray.Direction = L_shadow;
            ray.TMin = 0.01f;
            ray.TMax = dist - 0.05f;

            q.TraceRayInline(gTLAS, 0, 0xFF, ray);
            q.Proceed();

            if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
            {
                // Shadowed
                continue;
            }

            // --- FULL PBR SHADING (Cook-Torrance) ---
            // This matches the DXR logic you saw in the other example
            
            float3 H = normalize(V + L_central); // Half vector
            // Use diffuse color as the base light intensity/radiance
            float3 radiance = light.diffuseColor.rgb * attenuation;

            // 1. Fresnel (F) - How reflective is it at this angle?
            // F0: Dielectrics = 0.04, Metals = Albedo
            float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
            float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

            // 2. Normal Distribution (D) - The shape/sharpness of the highlight
            float NDF = DistributionGGX(normal, H, roughness);
            
            // 3. Geometry (G) - Microfacet self-shadowing
            float G = GeometrySmith(normal, V, L_central, roughness);

            // 4. Calculate Specular (The shiny reflection of the light source)
            float3 numerator = NDF * G * F;
            float denominator = 4.0 * max(dot(normal, V), 0.0) * NdotL + 0.0001; // +0.0001 prevents div by zero
            float3 specular = numerator / denominator;

            // 5. Diffuse (Energy Conservation)
            // Light that reflects (kS) cannot refract/diffuse (kD)
            float3 kS = F;
            float3 kD = float3(1.0, 1.0, 1.0) - kS;
            kD *= (1.0 - metalness); // Pure metals have 0 diffuse

            // 6. Combine and Add to Pixel
            // Note: dividing albedo by PI is standard for physically correct diffuse
            finalColor += (kD * albedo / PI + specular) * radiance * NdotL;
        }
    }
    
//    float3 reflectionColor = float3(0, 0, 0);
//    if (metalness > 0.1f || roughness < 0.5f)
//    {
//        float3 R = reflect(-V, N);
        
//        RayDesc rayReflect;
//        rayReflect.Origin = worldPos + N * 0.05f; // Bias to prevent self-intersection
//        rayReflect.Direction = R; // The reflection vector calculated above
//        rayReflect.TMin = 0.0f;
//        rayReflect.TMax = 1000.0f; // Far distance

//        RayQuery < RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES > qReflect;
//        qReflect.TraceRayInline(gTLAS, 0, 0xFF, rayReflect);
//        qReflect.Proceed();

//        if (qReflect.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
//        {
//        // WE HIT ANOTHER OBJECT!
//        // This is the tricky part of RTR. We know we hit something, 
//        // but what color is it *after Lighting*?
        
//        // Option A (Simple, temporary): Just return a solid color to prove it works.
//        reflectionColor = float3(0.05, 0.05, 0.05); // Reflected objects appear red

//        // Option B (Better, but complex): You need to fetch the Albedo/Material 
//        // of the hit triangle and light it. This usually requires recursion 
//        // (expensive) or sampling a pre-lit structure.
//        // A common hacky start is to sample the previous frame's color buffer 
//        // using screen-space reprojection, but that's advanced.

//        // Option C (Standard PBR approach): Sample an Environment Cube Map (HDRI).
//        // If you hit geometry, it occludes the skybox. If you miss, sample the skybox.
//        }
//        else
//        {
//        // WE MISSED (Hit the sky)
//        // Normally you sample an HDRI skybox texture here.
//            float3 sky = GetSkyColor(R);
//            reflectionColor = sky * (1.0f - roughness);
//        }
        
//        //reflectionColor *= (1.0 - roughness); // Rougher surfaces have dimmer reflections)
//    }
    
//    // Apply Fresnel (Metals reflect more at glancing angles, but are colored at facing angles)
//// Simplified Fresnel for metal: F0 is the albedo color.
//    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
//    float3 F = FresnelSchlick(max(dot(normal, V), 0.0f), F0);
    
//    finalColor += reflectionColor * F;
    
    finalColor += gEmissive.Load(uint3(pixel, 0)).rgb;
    //finalColor *= 5;
    //finalColor = finalColor / (finalColor + float3(1.0f, 1.0f, 1.0f)); // Simple tonemapping
    
    //finalColor = float3(metalness, metalness, metalness);
    //finalColor = float3(roughness, roughness, roughness);
    gOutput[pixel] = float4(finalColor, 1.0f);
    //float NdotL_Debug = max(dot(normal, normalize(-float3(-0.5, -1.0, -0.5))), 0.0f);
    //gOutput[pixel] = float4(NdotL_Debug, NdotL_Debug, NdotL_Debug, 1.0f);
}