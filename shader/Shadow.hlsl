
cbuffer ShadowConstants : register(b0)
{
    row_major float4x4 gWorld;               // Локальные коорды
    row_major float4x4 gLightViewProjection; // Мировая проекция 
};

struct VSInput
{
    float3 position : POSITION;
};

float4 VSMain(VSInput input) : SV_POSITION
{
    // Позиция сначала переносится в мир, затем в clip space виртуальной камеры солнца.
    float4 worldPosition = mul(float4(input.position, 1.0f), gWorld);
    return mul(worldPosition, gLightViewProjection);
}
