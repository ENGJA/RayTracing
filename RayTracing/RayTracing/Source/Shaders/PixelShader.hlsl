Texture2D    gAlbedo    : register(t0);
SamplerState gSampler   : register(s0);

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 normalWS : TEXCOORD2;
};

// Light struct matching C++ ConstantBufferData::LightData (position,color,dirType)
struct Light
{
    float4 position;
    float4 color;    // .xyz = color, .w = intensity
    float4 dirType;  // .xyz = direction (direction of rays), .w = type flag (1 = directional)
};

cbuffer CBData : register(b0)
{
    float4x4 vpMatrix;
    float4 viewPos;
    int numLights;
    float3 _pad;
    Light lights[8];
};

float4 main(PSInput input) : SV_TARGET
{
    float3 albedo = gAlbedo.Sample(gSampler, input.uv).rgb;

    float3 N = normalize(input.normalWS);
    float3 V = normalize(viewPos.xyz - input.worldPos);

    float3 finalColor = float3(0.0, 0.0, 0.0);

    // ambient
    const float ambientStrength = 0.4f;
    float3 ambient = ambientStrength * albedo;
    finalColor += ambient;

    int active = min(numLights, 8);
    for (int i = 0; i < active; ++i)
    {
        float3 L;
        // jeœli dirType.w == 1 => directional; dirType.xyz to kierunek promieni (gdzie promienie lec¹)
        if (lights[i].dirType.w > 0.5f)
        {
            // L ma byæ wektorem od powierzchni do Ÿród³a: to minus kierunku promieni
            L = normalize(-lights[i].dirType.xyz);
        }
        else
        {
            // standardowe punktowe Ÿród³o
            L = normalize(lights[i].position.xyz - input.worldPos);
        }

        float intensity = lights[i].color.w;
        float3 baseLightCol = lights[i].color.xyz;
        float3 lightCol = baseLightCol * intensity;

        float NdotL = saturate(dot(N, L));
        float3 diffuse = NdotL * albedo * lightCol;

        // Phong specular
        float3 R = reflect(-L, N);
        float spec = pow(saturate(dot(V, R)), 32.0f);
        float3 specular = spec * lightCol;

        finalColor += diffuse + specular;
    }

    return float4(finalColor, 1.0f);
}