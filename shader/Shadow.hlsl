cbuffer ShadowConstants : register(b0)
{
    row_major float4x4 gWorld;
    row_major float4x4 gLightViewProjection;
};

struct VSInput
{
    float3 position : POSITION;
};

float4 VSMain(VSInput input) : SV_POSITION
{
    float4 worldPosition = mul(float4(input.position, 1.0f), gWorld);
    return mul(worldPosition, gLightViewProjection);
}
