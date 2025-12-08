// Inputs
Texture2D<float4> gNoisyInput : register(t0); // Current frame (Ray Tracing Output)
Texture2D<float4> gHistoryInput : register(t1); // Previous frame (Accumulated)

// Output
RWTexture2D<float4> gOutput : register(u0); // Result to screen and next history

cbuffer DenoiseCB : register(b0)
{
    float blendFactor; // 0.05 for static, 1.0 for moving (resets history)
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    gOutput.GetDimensions(w, h);
    if (id.x >= w || id.y >= h)
        return;

    float3 current = gNoisyInput[id.xy].rgb;
    float3 history = gHistoryInput[id.xy].rgb;
    
    // --- VARIANCE CLIPPING (Better for HDR) ---
    float3 m1 = float3(0, 0, 0); // First moment (Mean)
    float3 m2 = float3(0, 0, 0); // Second moment (Variance)
    
    // Sample 3x3 Neighborhood
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            int2 pos = clamp(int2(id.xy) + int2(x, y), int2(0, 0), int2(w - 1, h - 1));
            float3 c = gNoisyInput[pos].rgb;
            m1 += c;
            m2 += c * c;
        }
    }

    // Average
    float3 mu = m1 / 9.0;
    float3 sigma = sqrt(abs(m2 / 9.0 - mu * mu));

    // Define a "valid box" based on standard deviation
    // Increasing the multiplier (e.g., 2.0 or 4.0) makes it more stable but might trail more.
    float3 minColor = mu - 1.5 * sigma;
    float3 maxColor = mu + 1.5 * sigma;

    // Clamp History to this box
    float3 clampedHistory = clamp(history, minColor, maxColor);

    // --- BLEND ---
    float3 result = lerp(clampedHistory, current, blendFactor);
    
    // Safety
    if (any(isnan(result)) || any(isinf(result)))
        result = float3(0, 0, 0);

    gOutput[id.xy] = float4(result, 1.0f);
}