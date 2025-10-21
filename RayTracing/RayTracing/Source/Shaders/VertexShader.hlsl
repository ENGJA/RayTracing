
struct VSInput
{
    float3 pos : POSITION;
    float4 color : COLOR;
};

struct VSOutput
{
    float4 pos : SV_POSITION; // Must always be here
    float4 color : COLOR; // Passing color
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
    output.color = input.color;
    return output;
}