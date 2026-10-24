cbuffer FrameCB : register(b0)
{
    column_major float4x4 view;
    column_major float4x4 projection;
};

cbuffer ObjectCB : register(b1)
{
    column_major float4x4 world;
};

struct VertexInput
{
    float3 position : POSITION;
    float4 color : COLOR;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(float4(input.position, 1.0f), world);
    output.position = mul(output.position, view);
    output.position = mul(output.position, projection);
    output.color = input.color;
    return output;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
    return float4(input.color.rgb, 1.0f);
}
