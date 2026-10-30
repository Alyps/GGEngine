struct LightData
{
    float3 position;
    uint type;
    float3 direction;
    float range;
    float3 color;
    float intensity;
    float4 extraParams;
};

cbuffer FrameCB : register(b0)
{
    column_major float4x4 view;
    column_major float4x4 projection;
    float3 cameraPosition;
    uint lightCount;
    float3 ambient;
    float padding;
    LightData lights[16];
};

cbuffer ObjectCB : register(b1)
{
    column_major float4x4 world;
    column_major float4x4 normalMatrix;
    float4 specular; // x: сила блика, y: показатель блеска, z: наличие diffuse.
};

Texture2D diffuseTexture : register(t0);
SamplerState diffuseSampler : register(s0);

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

// Нулевой вектор не должен давать NaN в центре точечного источника.
float3 SafeNormalize(float3 value)
{
    return value * rsqrt(max(dot(value, value), 1e-12f));
}

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    float4 position = mul(float4(input.position, 1.0f), world);
    output.worldPosition = position.xyz;
    output.position = mul(mul(position, view), projection);

    // Нормаль переносим в мировой базис без смещения.
    output.normal = mul(float4(input.normal, 0.0f), normalMatrix).xyz;
    output.uv = input.uv;
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float3 normal = SafeNormalize(input.normal);
    float3 toCamera = SafeNormalize(cameraPosition - input.worldPosition);
    // Материал без текстуры остаётся белым и получает обычное освещение.
    float4 baseColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (specular.z > 0.5f)
        baseColor = diffuseTexture.Sample(diffuseSampler, input.uv);
    float3 result = ambient * baseColor.rgb;

    // Фон учитывается один раз; каждый источник добавляет diffuse и specular.
    [loop]
    for (uint i = 0; i < min(lightCount, 16u); ++i)
    {
        LightData light = lights[i];
        float3 toLight = float3(0.0f, 0.0f, 0.0f);
        float attenuation = 1.0f;
        if (light.type == 0u)
        {
            toLight = SafeNormalize(-light.direction);
        }
        else if (light.type == 1u)
        {
            float3 offset = light.position - input.worldPosition;
            float distanceSquared = dot(offset, offset);
            if (light.range <= 0.0f || distanceSquared >= light.range * light.range)
                continue;

            // Плавно гасим свет на границе радиуса; центр остаётся конечным.
            float fade = saturate(1.0f - distanceSquared / (light.range * light.range));
            attenuation = fade * fade / (1.0f + distanceSquared);
            toLight = SafeNormalize(offset);
        }
        else
        {
            continue;
        }

        float diffuse = saturate(dot(normal, toLight));
        // На отвернутой от света поверхности зеркального блика нет.
        float highlight = 0.0f;
        if (diffuse > 0.0f)
            highlight = specular.x * pow(saturate(dot(reflect(-toLight, normal), toCamera)), specular.y);
        result += (baseColor.rgb * diffuse + highlight) * light.color * light.intensity * attenuation;
    }
    return float4(result, baseColor.a);
}
