Texture2D<float3> inputColor : register(t0);
RWTexture2D<float4> outputColor : register(u0);

[numthreads(8, 8, 1)]
void main( uint3 id : SV_DispatchThreadID )
{
    float3 color = inputColor.Load(int3(id.xy, 0));
    
    // Simple Reinhard tone mapping
    color /= (color + 1.0);
    // Gamma correction
    color = pow(color, 1.0 / 2.2);
    
    outputColor[id.xy] = float4(color, 1.0);
}