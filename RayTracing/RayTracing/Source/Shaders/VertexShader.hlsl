struct VSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float2 materialProps : TEXCOORD1; // x = metalness, y = shininess
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float3 worldPos : TEXCOORD1; // world-space position forwarded to PS
    float3 normalWS : TEXCOORD2; // world-space normal forwarded to PS
    float2 materialProps : TEXCOORD3; // forwarded per-vertex material props
};

// Light struct must match PixelShader and C++ layout to keep CB layout consistent
struct Light
{
    float4 position;
    float4 color;
    float4 dirType;
};

cbuffer CBData : register(b0)
{
    float4x4 vpMatrix;
    float4 viewPos; // not used in VS, but layout must match PS
    int numLights;
    float3 _pad;
    Light lights[25];
};

VSOutput main(VSInput input)
{
    VSOutput output;
    // Assuming vertex positions are already in world-space (Model::processMesh applies transform).
    output.worldPos = input.pos;
    output.normalWS = input.normal;
    output.materialProps = input.materialProps;
    output.pos = mul(vpMatrix, float4(input.pos, 1.0f));
    output.uv  = input.uv;
    return output;
}