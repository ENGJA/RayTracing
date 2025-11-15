Texture2D    gTexture    : register(t0);
SamplerState  gSampler    : register(s0);

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return gTexture.Sample(gSampler, input.uv);
}