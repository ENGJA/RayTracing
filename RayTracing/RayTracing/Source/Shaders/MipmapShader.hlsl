// MipmapShader.hlsl
// Generates one mip level from the previous level via 2x2 box filter.
// Bindings expected:
//  - b0: MipConstants
//  - t0: SRV of the source texture (viewed at SrcMipLevel)
//  - u0: UAV of the destination mip level (exact mip bound as UAV)
//  - s0: Point sampler (optional, not used with Load())

cbuffer MipConstants : register(b0)
{
    uint2 SrcSize;       // Size of the source mip level (width,height)
    uint  SrcMipLevel;   // Mip level index of the source being read
    uint  _Padding;      // Unused padding for 16-byte alignment
};

// Source mip level (full texture SRV, we specify mip with Load())
Texture2D<float4> gSrc : register(t0);
// Destination mip level UAV (this UAV is bound to the target mip only)
RWTexture2D<float4> gDst : register(u0);

// Thread group covers an 8x8 tile of destination pixels.
[numthreads(8,8,1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Derive destination size from source (each mip halves, but minimum 1)
    uint2 dstSize = uint2(max(SrcSize.x >> 1, 1), max(SrcSize.y >> 1, 1));

    if (DTid.x >= dstSize.x || DTid.y >= dstSize.y)
        return; // Out of bounds for this mip

    uint2 baseSrc = DTid.xy * 2; // Top-left of 2x2 block in source mip

    // Clamp to source bounds to handle odd dimensions
    uint2 srcMax = SrcSize - 1;
    uint2 p00 = min(baseSrc + uint2(0,0), srcMax);
    uint2 p10 = min(baseSrc + uint2(1,0), srcMax);
    uint2 p01 = min(baseSrc + uint2(0,1), srcMax);
    uint2 p11 = min(baseSrc + uint2(1,1), srcMax);

    float4 c00 = gSrc.Load(int3(p00, SrcMipLevel));
    float4 c10 = gSrc.Load(int3(p10, SrcMipLevel));
    float4 c01 = gSrc.Load(int3(p01, SrcMipLevel));
    float4 c11 = gSrc.Load(int3(p11, SrcMipLevel));

    float4 avg = (c00 + c10 + c01 + c11) * 0.25f;

    gDst[DTid.xy] = avg;
}