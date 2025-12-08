struct VSOutput
{
    float4 pos : SV_POSITION;
    float4 curPosHS : POSITION0; // Homogeneous Space
    float4 prevPosHS : POSITION1;
};

// You need a Constant Buffer with BOTH matrices
cbuffer CB : register(b0)
{
    float4x4 viewProj;
    float4x4 prevViewProj; // The VP matrix from the previous frame
};

VSOutput main(float3 pos : POSITION)
{
    VSOutput output;
    float4 worldPos = float4(pos, 1.0);
    
    output.pos = mul(viewProj, worldPos);
    output.curPosHS = output.pos;
    output.prevPosHS = mul(prevViewProj, worldPos); // Calculate where this vertex WAS
    return output;
}