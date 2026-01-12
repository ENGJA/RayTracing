struct VSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
    float2 materialProps : TEXCOORD1; // x = metalness, y = shininess
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 worldPos : TEXCOORD1; // world-space position forwarded to PS
    float3 normalWS : TEXCOORD2; // world-space normal forwarded to PS
    float4 tangentWS : TEXCOORD3; // world-space tangent forwarded to PS
    float2 materialProps : TEXCOORD4; // forwarded per-vertex material props
};

// Light struct must match PixelShader and C++ layout to keep CB layout consistent
struct Light
{
    float4 position;
    float4 dirType; // .xyz = direction (direction of rays), .w = type flag (1 = directional)
    //float4 color;    // .xyz = color, .w = intensity
    float4 diffuseColor;
    float4 specularColor;
};

//cbuffer CBData : register(b0)
//{
//    float4x4 vpMatrix : packoffset(c0);
//    float4 viewPos : packoffset(c4);
//    int numLights : packoffset(c5.x);
//    float3 _pad : packoffset(c5.y);
//    Light lights[25] : packoffset(c6);
//};


cbuffer CBData : register(b0)
{
    float4x4 vpMatrix;
    float4x4 invViewProj;
    
    float3 viewPos;
    int numLights;
    
    int numPointLights;
    int frameCount;
    int shadowsEnabled;
    int reflectionsEnabled;
    
    int maxReflectionDepth;
    int maxTransparentDepth;
    int risCandidates;
    int shadowRays;
    
    float nearZ;
    float farZ;
    float2 _pad0;
    
    Light lights[25];
};

cbuffer MaterialData : register(b1)
{
    float4 gBaseColorFactor : packoffset(c0);
    float gMetalnessFactor : packoffset(c1.x);
    float gRoughnessFactor : packoffset(c1.y);
    float gAlphaCutoff : packoffset(c1.z);
    float _pad2 : packoffset(c1.w);
    float4 gEmissiveFactor : packoffset(c2);
};

VSOutput main(VSInput input)
{
    VSOutput output;
    // Assuming vertex positions are already in world-space (Model::processMesh applies transform).
    output.worldPos = input.pos;
    output.normalWS = input.normal;
    output.tangentWS = input.tangent;
    output.materialProps = input.materialProps;
    output.pos = mul(vpMatrix, float4(input.pos, 1.0f));
    output.uv = input.uv;
    return output;
}