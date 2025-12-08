
// --- RESOURCES ---
Texture2D<float4> gNoisyInput : register(t0); // Current Frame (Ray Tracing Output)
Texture2D<float4> gHistoryInput : register(t1); // Previous Frame (Accumulated)
Texture2D<float2> gMotionVectors : register(t2); // NEW: Velocity Buffer (R16G16_FLOAT)
RWTexture2D<float4> gOutput : register(u0);

// NEW: Bilinear Sampler needed for sub-pixel reprojection
SamplerState gLinearClamp : register(s0);

cbuffer DenoiseCB : register(b0)
{
    float gBaseBlendFactor; // Default: 0.05 (Keep 95% history)
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    gOutput.GetDimensions(w, h);
    if (id.x >= w || id.y >= h)
        return;

    float2 texelSize = 1.0f / float2(w, h);
    float2 uv = (id.xy + 0.5f) * texelSize;

    // 1. Read Motion Vector
    // Velocity = (CurrentUV - PreviousUV). 
    // To find history, we subtract: PreviousUV = CurrentUV - Velocity
    float2 velocity = gMotionVectors[id.xy].xy;
    float2 prevUV = uv - velocity;

    // 2. Read Current Color
    float3 current = gNoisyInput[id.xy].rgb;

    // 3. Read History (Reprojected)
    float3 history = current; // Fallback to current if history is invalid
    bool historyValid = false;

    // Check if the previous position was inside the screen
    if (all(prevUV >= 0.0f) && all(prevUV <= 1.0f))
    {
        // Sample History with Bilinear Interpolation
        // (Must use SampleLevel in Compute Shader)
        history = gHistoryInput.SampleLevel(gLinearClamp, prevUV, 0).rgb;
        historyValid = true;
    }

    // 4. Variance Clipping (The "Anti-Ghosting" Logic)
    // Even with reprojection, history might be wrong (e.g. shadows moved, lighting changed).
    // We inspect the 3x3 neighborhood of the CURRENT frame to define a "safe color box".
    
    float3 m1 = float3(0, 0, 0); // Mean
    float3 m2 = float3(0, 0, 0); // Variance

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            int2 pos = clamp(int2(id.xy) + int2(x, y), int2(0, 0), int2(w - 1, h - 1));
            float3 neighbor = gNoisyInput[pos].rgb;
            m1 += neighbor;
            m2 += neighbor * neighbor;
        }
    }

    float3 mu = m1 / 9.0f;
    float3 sigma = sqrt(abs(m2 / 9.0f - mu * mu));

    // Define valid range (Mean +/- 1.5 StdDev)
    float3 minColor = mu - 1.5f * sigma;
    float3 maxColor = mu + 1.5f * sigma;

    // Clamp the reprojected history to this box.
    // If history is "ghosting" (color that doesn't exist anymore), this forces it 
    // to look like the current frame.
    float3 clampedHistory = clamp(history, minColor, maxColor);

    // 5. Final Blend
    // If history was off-screen, force blend to 1.0 (replace with current)
    float blend = historyValid ? gBaseBlendFactor : 1.0f;
    
    float3 result = lerp(clampedHistory, current, blend);
    
    // NaNs/Infs Killer
    if (any(isnan(result)) || any(isinf(result)))
        result = float3(0, 0, 0);

    gOutput[id.xy] = float4(result, 1.0f);
}