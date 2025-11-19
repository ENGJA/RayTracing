struct VSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

struct CBData
{
    float4x4 vpMatrix;
};

ConstantBuffer<CBData> cb : register(b0);

VSOutput main(VSInput input)
{
    VSOutput output;
    output.pos = mul(cb.vpMatrix, float4(input.pos, 1.0f));
    output.uv  = input.uv;
    return output;
}