#pragma once

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

float GeometrySchlickGGX_Direct(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0; // Analytic Direct Light Formula
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySchlickGGX_IBL(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0; // IBL / Reflection Formula
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness, bool isIBL)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    
    if (isIBL)
    {
        return GeometrySchlickGGX_IBL(NdotV, roughness) * GeometrySchlickGGX_IBL(NdotL, roughness);
    }
    else
    {
        return GeometrySchlickGGX_Direct(NdotV, roughness) * GeometrySchlickGGX_Direct(NdotL, roughness);
    }
}

//float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
//{
//    float NdotV = max(dot(N, V), 0.0);
//    float NdotL = max(dot(N, L), 0.0);
//    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
//}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

// --- HELPER: PBR Sampling ---
//float3 GetTangentBasis(float3 N)
//{
//    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
//    float3 T = normalize(cross(up, N));
//    float3 B = cross(N, T);
//    return float3(T, B, N); // T, B, N basis
//}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    
    // Spherical to Cartesian
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
    
    // Tangent to World Space
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    
    float3 sampleDir = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleDir);
}

struct Light
{
    float4 position;
    float4 dirType; // .xyz = direction (direction of rays), .w = type flag (1 = directional)
    //float4 color;    // .xyz = color, .w = intensity
    float4 diffuseColor;
    float4 specularColor;
};


float3 ImportanceSampleGGX_Transmission(float2 Xi, float3 N, float roughness, float ior, out float pdf)
{
    float a = roughness * roughness;
    
    // 1. Sample Microfacet Normal (H) - Same as Reflection
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
    
    // Tangent Space to World Space
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    float3 H_world = normalize(tangent * H.x + bitangent * H.y + N * H.z);
    
    pdf = (2.0 * PI) / (1.0 + (a * a - 1.0) * Xi.y); // Simplified PDF for brevity
    
    return H_world;
}