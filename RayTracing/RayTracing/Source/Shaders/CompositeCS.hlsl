RWTexture2D<float4> gOutput : register(u0); // Backbuffer

Texture2D<float4> gDenoisedDiffuse : register(t0);
Texture2D<float4> gDenoisedSpecular : register(t1);
Texture2D<float4> gAlbedo : register(t2);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // 1. Load Data
    float3 diffuse = gDenoisedDiffuse[id.xy].rgb;
    float3 specular = gDenoisedSpecular[id.xy].rgb;
    float3 albedo = gAlbedo[id.xy].rgb;

    // 2. Remodulate (The "Lighting Equation")
    // Final = (Diffuse * Albedo) + Specular
    float3 finalColor = (diffuse * albedo) + specular;

    // 3. Simple Tone Mapping (Reinhard) & Gamma
    finalColor = finalColor / (finalColor + 1.0f);
    finalColor = pow(finalColor, 1.0f / 2.2f);

    gOutput[id.xy] = float4(finalColor, 1.0f);
}