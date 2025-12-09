// ToneMapCS.hlsl
Texture2D<float4> gInput : register(t0);
RWTexture2D<unorm float4> gOutput : register(u0); // 'unorm' handles the float->int conversion

// ACES Tone Mapping Curve (Standard for games)
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint width, height;
    gInput.GetDimensions(width, height);
    if (DTid.x >= width || DTid.y >= height)
        return;

    float4 hdr = gInput[DTid.xy];

    // 1. Tone Map (HDR -> LDR 0..1 range)
    float3 ldr = ACESFilm(hdr.rgb);

    // 2. Gamma Correction (Linear -> sRGB)
    // Most monitors need this! 1.0/2.2 = 0.4545
    ldr = pow(ldr, 1.0 / 2.2);

    gOutput[DTid.xy] = float4(ldr, 1.0f);
}