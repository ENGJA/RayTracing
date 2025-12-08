struct VSOutput
{
    float4 pos : SV_POSITION;
    float4 curPosHS : POSITION0; // Homogeneous Space
    float4 prevPosHS : POSITION1;
};

float4 main(VSOutput input) : SV_Target
{
    // 1. Convert to NDC (Normalized Device Coordinates) [-1 to 1]
    float2 curNDC = input.curPosHS.xy / input.curPosHS.w;
    float2 prevNDC = input.prevPosHS.xy / input.prevPosHS.w;

    // 2. Convert to UV [0 to 1]
    // Note: D3D UVs have Y pointing down. NDC Y points up.
    float2 curUV = curNDC * float2(0.5, -0.5) + 0.5;
    float2 prevUV = prevNDC * float2(0.5, -0.5) + 0.5;

    // 3. Calculate Velocity
    float2 velocity = curUV - prevUV;

    return float4(velocity, 0, 0); // Write to R16G16_FLOAT
}