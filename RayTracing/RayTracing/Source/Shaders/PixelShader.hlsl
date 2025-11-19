Texture2D    gAlbedo    : register(t0);
Texture2D    gNormal    : register(t1);
Texture2D    gMetalness : register(t2);
Texture2D    gRoughness : register(t3);
Texture2D    gEmissive  : register(t4);
SamplerState gSampler   : register(s0);

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    // Sample albedo only for now; other textures available for future PBR shading
    float4 baseColor = gAlbedo.Sample(gSampler, input.uv);
    return baseColor;
}