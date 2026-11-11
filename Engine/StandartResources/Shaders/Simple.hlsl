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
    float2 specular;
    uint textureMask; // Бит 0: Base Color, бит 1: AO.
    uint normalEncoding; // 0: пусто, 1: UNORM RGB, 2: UNORM XY, 3: SNORM XY, 4: SNORM RGB.
};

Texture2D diffuseTexture : register(t0);
Texture2D normalTexture : register(t1);
Texture2D aoTexture : register(t2);
SamplerState materialSampler : register(s0);

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

// Отдельная точка входа компилируется только для оболочек редактора.
float4 PSColliderMain(VertexOutput input) : SV_TARGET
{
    return float4(0.15f, 0.9f, 0.35f, 0.22f);
}

float3 ApplyNormalMap(VertexOutput input, float3 normal)
{
    // Направления U и V вычисляем по производным мировой позиции и UV.
    float3 positionX = ddx(input.worldPosition);
    float3 positionY = ddy(input.worldPosition);
    float2 uvX = ddx(input.uv);
    float2 uvY = ddy(input.uv);
    float determinant = uvX.x * uvY.y - uvX.y * uvY.x;
    float uvScale = length(uvX) * length(uvY);
    float3 mapped = normal;
    if (abs(determinant) > 1e-5f * max(uvScale, 1e-12f))
    {
        float3 tangent = (positionX * uvY.y - positionY * uvX.y) / determinant;
        float3 directionV = (positionY * uvX.x - positionX * uvY.x) / determinant;
        // Базис ортогонализуем к интерполированной нормали и учитываем зеркальные UV.
        tangent = SafeNormalize(tangent - normal * dot(normal, tangent));
        float3 bitangent = cross(normal, tangent);
        if (dot(bitangent, directionV) < 0.0f)
            bitangent = -bitangent;

        // Для двухканальных карт восстанавливаем Z. После ветвления нужны явные градиенты.
        float3 sampled = normalTexture.SampleGrad(materialSampler, input.uv, uvX, uvY).xyz;
        if (normalEncoding == 1u || normalEncoding == 2u)
            sampled = sampled * 2.0f - 1.0f;
        if (normalEncoding == 2u || normalEncoding == 3u)
            sampled.z = sqrt(saturate(1.0f - dot(sampled.xy, sampled.xy)));
        float3 direction = tangent * sampled.x + bitangent * sampled.y + normal * sampled.z;
        if (dot(bitangent, bitangent) > 1e-6f && dot(direction, direction) > 1e-12f)
            mapped = SafeNormalize(direction);
    }
    return mapped;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    float3 normal = SafeNormalize(input.normal);
    if (normalEncoding != 0u)
        normal = ApplyNormalMap(input, normal);
    float3 toCamera = SafeNormalize(cameraPosition - input.worldPosition);
    // Материал без текстуры остаётся белым и получает обычное освещение.
    float4 baseColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if ((textureMask & 1u) != 0u)
        baseColor = diffuseTexture.Sample(materialSampler, input.uv);
    // AO ослабляет фоновый свет. Прямой свет по-прежнему зависит от источника.
    float ao = 1.0f;
    if ((textureMask & 2u) != 0u)
        ao = saturate(aoTexture.Sample(materialSampler, input.uv).r);
    float3 result = ambient * baseColor.rgb * ao;

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
