Texture2D    gAlbedo              : register(t0);
Texture2D    gMetalness           : register(t1);
Texture2D    gRoughness           : register(t2); 
Texture2D    gNormalMap           : register(t3);
Texture2D    gEmissive            : register(t4);
SamplerState gSampler             : register(s0);

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 normalWS : TEXCOORD2;
    float2 materialProps : TEXCOORD3;
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
    Light lights[25];
};

float4 main(PSInput input) : SV_TARGET
{
    float3 albedo = gAlbedo.Sample(gSampler, input.uv).rgb;

    float texMetal = gMetalness.Sample(gSampler, input.uv).r;
    float texRough = gRoughness.Sample(gSampler, input.uv).r;

    float metalness = texMetal;
    float roughness = texRough;

    if (metalness == 0.0f)
        metalness = saturate(input.materialProps.x);

    float shininessFromRough = lerp(8.0f, 2048.0f, 1.0f - saturate(roughness));
    float shininess = (input.materialProps.y > 0.0f) ? input.materialProps.y : shininessFromRough;

    float3 N = normalize(input.normalWS);
    float3 V = normalize(viewPos.xyz - input.worldPos);

    float3 finalColor = float3(0.0, 0.0, 0.0);

    // ambient
    const float ambientStrength = 0.2f;
    float3 ambient = ambientStrength * albedo;
    finalColor += ambient;

    int active = min(numLights, 25);
    for (int i = 0; i < active; ++i)
    {
        float3 L;
        bool isDirectional = (lights[i].dirType.w > 0.5f);

        if (isDirectional)
        {
            L = normalize(-lights[i].dirType.xyz);
        }
        else
        {
            L = normalize(lights[i].position.xyz - input.worldPos);
        }

        float intensity = lights[i].color.w;
        float3 baseLightCol = lights[i].color.xyz;
        float3 lightCol = baseLightCol * intensity;

        // Attenuation for point lights
        float attenuation = 1.0f;
        if (!isDirectional)
        {
            // Tunable constants (constant, linear, quadratic)
            const float kConst = 1.0f;
            const float kLinear = 0.5f;
            const float kQuadratic = 0.2f;

            float dist = length(lights[i].position.xyz - input.worldPos);
            float denom = kConst + kLinear * dist + kQuadratic * dist * dist;
            attenuation = 1.0f / max(denom, 1e-4f);
            // Optional: clamp to avoid extremely bright values
            attenuation = saturate(attenuation * 1.0f);
            lightCol *= attenuation;
        }

        float NdotL = saturate(dot(N, L));
        float3 diffuse = NdotL * albedo * lightCol;

        float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, saturate(metalness));

        float specShininess = shininess;

        // Phong specular
        float3 R = reflect(-L, N);
        float specFactor = pow(saturate(dot(V, R)), specShininess);
        float3 specular = specFactor * F0 * lightCol;

        finalColor += diffuse + specular;
    }
    float3 emissive = gEmissive.Sample(gSampler, input.uv).rgb;
    finalColor += emissive;

    return float4(finalColor, 1.0f);
}