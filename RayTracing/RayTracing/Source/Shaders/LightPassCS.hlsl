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

// 2. The Scene Structure (For Inline Ray Tracing)
RaytracingAccelerationStructure gTLAS : register(t4);

// 3. Many Lights Data (Structured Buffer instead of Constant Array)
StructuredBuffer<Light> gLights : register(t5);

// 4. Output (The final image or accumulation buffer)
RWTexture2D<float4> gOutput : register(u0);

cbuffer FrameCB : register(b0)
{
    float4x4 vpMatrix; // Offset 0 (Unused in Compute)
    float4x4 invViewProj;
    float3 viewPos;
    int numLights; // Can be thousands now
    float3 _pad;
};

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

static const float PI = 3.14159265359f;

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom + 0.0000001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

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
    // For now, we loop up to 128 lights per pixel.
    uint activeLights = min(numLights, 128);

    for (uint i = 0; i < activeLights; ++i)
    {
        Light light = gLights[i];
        float3 L;
        float dist;
        float attenuation = 1.0f;

        if (light.dirType.w > 0.5f) // Directional
        {
            L = normalize(-light.dirType.xyz);
            dist = 10000.0f; // Infinite
        }
        else // Point
        {
            float3 diff = light.position.xyz - worldPos;
            dist = length(diff);
            L = normalize(diff);
            
            // Simple Quadratic falloff
            attenuation = 1.0f / (1.0f + 0.1f * dist + 0.05f * dist * dist);
        }

        float NdotL = max(dot(normal, L), 0.0f);
        if (NdotL > 0.0f && attenuation > 0.001f)
        {
            // --- INLINE RAY TRACING SHADOWS ---
            RayQuery < RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES > q;
            RayDesc ray;
            ray.Origin = worldPos + normal * 0.05f; // Bias to prevent self-shadowing acne
            ray.Direction = L;
            ray.TMin = 0.0f;
            ray.TMax = dist - 0.05f;

            q.TraceRayInline(gTLAS, 0, 0xFF, ray);
            q.Proceed();

            if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
            {
                // Shadowed
                continue;
            }

            // --- PBR SHADING (Simplified) ---
            // Diffuse
            float3 diffuse = albedo * light.diffuseColor.rgb * NdotL * attenuation;
            
            // Specular
            float3 H = normalize(V + L);
            float NdotH = max(dot(N, H), 0.0f);
            float specPower = lerp(10.0f, 500.0f, 1.0f - roughness);
            float specFactor = pow(NdotH, specPower) * metalness;
            float3 specular = specFactor * light.specularColor.rgb * attenuation;

            finalColor += diffuse + specular;
        }
    }
    
    float3 reflectionColor = float3(0, 0, 0);
    if (metalness > 0.1f || roughness < 0.5f)
    {
        float3 R = reflect(-V, N);
        
        RayDesc rayReflect;
        rayReflect.Origin = worldPos + N * 0.05f; // Bias to prevent self-intersection
        rayReflect.Direction = R; // The reflection vector calculated above
        rayReflect.TMin = 0.0f;
        rayReflect.TMax = 1000.0f; // Far distance

        RayQuery < RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES > qReflect;
        qReflect.TraceRayInline(gTLAS, 0, 0xFF, rayReflect);
        qReflect.Proceed();

        if (qReflect.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
        // WE HIT ANOTHER OBJECT!
        // This is the tricky part of RTR. We know we hit something, 
        // but what color is it *after Lighting*?
        
        // Option A (Simple, temporary): Just return a solid color to prove it works.
        reflectionColor = float3(0.05, 0.05, 0.05); // Reflected objects appear red

        // Option B (Better, but complex): You need to fetch the Albedo/Material 
        // of the hit triangle and light it. This usually requires recursion 
        // (expensive) or sampling a pre-lit structure.
        // A common hacky start is to sample the previous frame's color buffer 
        // using screen-space reprojection, but that's advanced.

        // Option C (Standard PBR approach): Sample an Environment Cube Map (HDRI).
        // If you hit geometry, it occludes the skybox. If you miss, sample the skybox.
        }
        else
        {
        // WE MISSED (Hit the sky)
        // Normally you sample an HDRI skybox texture here.
            reflectionColor = GetSkyColor(R);
        }
        
        //reflectionColor *= (1.0 - roughness); // Rougher surfaces have dimmer reflections)
    }
    
    // Apply Fresnel (Metals reflect more at glancing angles, but are colored at facing angles)
// Simplified Fresnel for metal: F0 is the albedo color.
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
    float3 F = FresnelSchlick(max(dot(normal, V), 0.0f), F0);
    
    finalColor += reflectionColor * F;
    
    //finalColor = float3(metalness, metalness, metalness);
    //finalColor = float3(roughness, roughness, roughness);
    gOutput[pixel] = float4(finalColor, 1.0f);
    //float NdotL_Debug = max(dot(normal, normalize(-float3(-0.5, -1.0, -0.5))), 0.0f);
    //gOutput[pixel] = float4(NdotL_Debug, NdotL_Debug, NdotL_Debug, 1.0f);
}