#include "PBR.hlsli"

Texture2D<float3> gDirectLight : register(t0);
Texture2D<float3> gReflections : register(t1);
Texture2D<float3> gAlbedo : register(t2);
//Texture2D<float3> gAlbedoSpecular : register(t3); // Unused
Texture2D<float3> gEmissive : register(t4);

RWTexture2D<float4> gOutput : register(u0);

cbuffer CB : register(b0) // unused
{
    float4x4 _vpMatrix; // Unused here, needed in other passes
    float4x4 invViewProj;
    float3 camPos;
    float _pad0;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    gOutput.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;    
    
    float3 direct = gDirectLight[id.xy];
    float3 refl = gReflections[id.xy];
    
    // Read G-Buffer
    float3 albedo = gAlbedo[id.xy];
    float3 emissive = gEmissive[id.xy].rgb;
    
    // Final Composition
    float3 finalColor = direct * albedo + refl + emissive;    
    gOutput[id.xy] = float4(finalColor, 1.0);
}