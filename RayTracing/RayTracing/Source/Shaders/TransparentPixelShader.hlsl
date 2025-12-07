#include "PBR.hlsli"

// --- CONSTANTS ---
// Match C++ Layout
struct LightData
{
    float4 position;
    float4 dirType;
    float4 diffuseColor;
    float4 specularColor;
};

cbuffer FrameCB : register(b0)
{
    float4x4 vpMatrix;
    float4x4 invViewProj;
    float3 viewPos;
    int numLights;
    int frameCount;
    float3 _pad;
    LightData lights[25];
};

cbuffer MaterialCB : register(b1)
{
    float4 gBaseColor;
    float gMetalness;
    float gRoughness;
    float gAlphaCutoff;
    float _padMat;
    float4 gEmissive;
};

// --- RESOURCES ---
Texture2D gAlbedo : register(t0);
Texture2D gMetalnessMap : register(t1);
Texture2D gRoughnessMap : register(t2);
Texture2D gNormalMap : register(t3);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 tangent : TEXCOORD3;
};

float4 main(PSInput input) : SV_TARGET
{
    // 1. Sample Material
    float4 albedoData = gAlbedo.Sample(gSampler, input.uv);
    float3 albedo = albedoData.rgb * gBaseColor.rgb;
    float alpha = albedoData.a * gBaseColor.a;
    
    float metalness = gMetalnessMap.Sample(gSampler, input.uv).r * gMetalness;
    float roughness = gRoughnessMap.Sample(gSampler, input.uv).r * gRoughness;
    
    // 2. Normal Mapping (Simplified for brevity, use your TBN function)
    float3 N = normalize(input.normal);
    float3 V = normalize(viewPos - input.worldPos);

    // 3. Lighting Loop (Forward)
    float3 finalColor = float3(0, 0, 0);
    // Simple ambient
    finalColor += albedo * 0.1f;

    for (int i = 0; i < min(numLights, 8); ++i) // Limit lights for transparency performance
    {
        LightData light = lights[i];
        float3 L;
        float attenuation = 1.0f;
        
        if (light.dirType.w > 0.5f)
        {
            L = normalize(-light.dirType.xyz);
        }
        else
        {
            float3 diff = light.position.xyz - input.worldPos;
            float dist = length(diff);
            L = normalize(diff);
            attenuation = 1.0f / (1.0f + 0.1f * dist + 0.05f * dist * dist);
        }

        float NdotL = max(dot(N, L), 0.0f);
        if (NdotL > 0.0f)
        {
            float3 H = normalize(V + L);
            float3 radiance = light.diffuseColor.rgb * attenuation;
            
            float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
            float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
            float NDF = DistributionGGX(N, H, roughness);
            float G = GeometrySmith(N, V, L, roughness);
            
            float3 num = NDF * G * F;
            float denom = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
            float3 specular = num / denom;
            
            float3 kS = F;
            float3 kD = (float3(1, 1, 1) - kS) * (1.0 - metalness);
            
            finalColor += (kD * albedo / PI + specular) * radiance * NdotL;
        }
    }

    return float4(finalColor, alpha);
}