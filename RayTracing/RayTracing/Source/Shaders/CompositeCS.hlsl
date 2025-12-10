#include "PBR.hlsli"

Texture2D<float4> gDirectLight : register(t0);
Texture2D<float4> gReflections : register(t1);
// Need G-Buffer to calculate Fresnel mixing
Texture2D<float4> gAlbedo : register(t2);
Texture2D<float3> gNormal : register(t3);
Texture2D<float2> gMaterial : register(t4);
Texture2D<float> gDepth : register(t5);

RWTexture2D<float4> gOutput : register(u0);

cbuffer CB : register(b0)
{
    float4x4 _vpMatrix; // Unused here, needed in other passes
    float4x4 invViewProj;
    float3 camPos;
    float _pad0;
};

// Reconstruct World Position from Depth
float3 GetWorldPosition(float2 uv, float depth)
{
    float4 clipSpace = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipSpace.y = -clipSpace.y; // Flip Y for DX coordinates
    float4 worldPos = mul(invViewProj, clipSpace);
    return worldPos.xyz / worldPos.w;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    gOutput.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;
    
    float2 uv = (id.xy + 0.5) / float2(width, height);
    
    float3 direct = gDirectLight[id.xy].rgb;
    float3 refl = gReflections[id.xy].rgb;
    
    // Read G-Buffer
    float3 N = gNormal[id.xy];
    float depth = gDepth[id.xy];
    float2 mats = gMaterial[id.xy];
    float3 albedo = gAlbedo[id.xy].rgb;
    
    // Reconstruct View Vector
    float3 worldPos = GetWorldPosition(uv, depth); // Need UV conversion logic here
    float3 V = normalize(camPos - worldPos);
    if (dot(N, V) < 0.0)
        N = -N; // Ensure normal faces view direction)

    // Fresnel Mix
    float3 F0 = lerp(0.04, albedo, mats.x); // mats.x = metalness
    float3 F = FresnelSchlick(max(dot(N, V), 0.0), F0);
    
    
    float3 finalColor = direct + refl * F;
    finalColor = finalColor / (finalColor + 1.0); // Reinhard tonemapping)
    finalColor = pow(finalColor, 1.0 / 2.2); // Gamma correction
    
    // Final Combine: Direct + (Reflection * Fresnel)
    gOutput[id.xy] = float4(finalColor, 1.0);
}