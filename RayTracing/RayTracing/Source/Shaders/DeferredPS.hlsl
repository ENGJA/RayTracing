Texture2D    gAlbedo              : register(t0);
Texture2D    gMetalness           : register(t1);
Texture2D    gRoughness           : register(t2); 
Texture2D    gNormalMap           : register(t3);
Texture2D    gEmissive            : register(t4);
SamplerState gSampler             : register(s0);

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 normalWS : TEXCOORD2;
    float4 tangentWS : TEXCOORD3;
    float2 materialProps : TEXCOORD4;
    
    bool isFrontFace : SV_IsFrontFace;
};

struct PSOutput
{
    float4 Albedo   : SV_TARGET0; // R8G8B8A8_UNORM
    float4 Normal   : SV_TARGET1; // R16G16B16A16_FLOAT
    float2 Material : SV_TARGET2; // R32G32_FLOAT (Metalness, Roughness)
    float4 Emissive : SV_TARGET3; // R16G16B16A16_FLOAT
};

// Light struct matching C++ ConstantBufferData::LightData (position,color,dirType)
struct Light
{
    float4 position;
    float4 dirType;  // .xyz = direction (direction of rays), .w = type flag (1 = directional)
    //float4 color;    // .xyz = color, .w = intensity
    float4 diffuseColor;
    float4 specularColor;
};

cbuffer CBData : register(b0)
{
    float4x4 vpMatrix : packoffset(c0);
    float4x4 invViewProj : packoffset(c4);
    float3 viewPos : packoffset(c8);
    int numLights : packoffset(c8.w);
    int numPointLights : packoffset(c9.x);
    
    int frameCount : packoffset(c9.y);
    int shadowsEnabled : packoffset(c9.z);
    int reflectionsEnabled : packoffset(c9.w);
    int maxRecursionDepth : packoffset(c10.x);
    float3 _pad0 : packoffset(c10.y);
    Light lights[25] : packoffset(c11);
};

cbuffer MaterialData : register(b1)
{
    float4 gBaseColorFactor : packoffset(c0);
    float gMetalnessFactor  : packoffset(c1.x);
    float gRoughnessFactor  : packoffset(c1.y);
    float gAlphaCutoff      : packoffset(c1.z);
    float _pad2             : packoffset(c1.w);
    float4 gEmissiveFactor  : packoffset(c2);
};


float3 getNormal(PSInput input);

PSOutput main(PSInput input)
{
    float4 albedoSample = gAlbedo.Sample(gSampler, input.uv);
    float3 albedo = albedoSample.rgb * gBaseColorFactor.rgb;
    float alpha = albedoSample.a * gBaseColorFactor.a;

    float4 emissiveSample = gEmissive.Sample(gSampler, input.uv);
    float3 emissive = emissiveSample.rgb * gEmissiveFactor.rgb;

    //#define ALPHA_TEST 1
#ifdef ALPHA_TEST
    clip(alpha - gAlphaCutoff); // Discard pixels with low alpha for alpha testing
#endif
    
    if (!input.isFrontFace)
    {
        input.normalWS = -input.normalWS;
    }

    float texRough = gMetalness.Sample(gSampler, input.uv).g;
    float texMetal = gMetalness.Sample(gSampler, input.uv).b;

    float metalness = texMetal * gMetalnessFactor;
    float roughness = texRough * gRoughnessFactor;

    if (metalness == 0.0f)
        metalness = saturate(input.materialProps.x);

    //float shininessFromRough = lerp(8.0f, 2048.0f, 1.0f - saturate(roughness));
    //float shininess = (input.materialProps.y > 0.0f) ? input.materialProps.y : shininessFromRough;
    
    float3 N = getNormal(input);    
    
    PSOutput output;
    output.Albedo = float4(albedo, alpha);
    output.Normal = float4(N, 0.0f);
    //output.Material = float2(0, 1);
    output.Material = float2(roughness, metalness);
    output.Emissive = float4(emissive, 1.0f);
    
    
    return output;
}

float3 getNormal(PSInput input)
{
    // Normalize normal and tangent vectors
    float3 N = normalize(input.normalWS);
    float3 T = normalize(input.tangentWS.xyz);      
    
    // Re-orthogonalize T with respect to N
    T = normalize(T - dot(T, N) * N);
    
    // Compute bitangent
    float3 B = cross(N, T) * input.tangentWS.w;
    
    // Construct TBN matrix
    float3x3 TBN = float3x3(T, B, N);
    
    // Transform normal map
    float3 normalMapSample = gNormalMap.Sample(gSampler, input.uv).xyz;
    float3 tangentNormal = normalize(normalMapSample * 2.0f - 1.0f);
    // If your normal map appears inverted along the Y axis, uncomment the following line to flip 
    //tangentNormal.y = -tangentNormal.y;
    
    float3 pixelNormal = normalize(mul(tangentNormal, TBN));
    return pixelNormal;
}