cbuffer ObjectConstants : register(b0)
{
    row_major float4x4 gWorld;
    row_major float4x4 gViewProjection;
    float4 gCameraAndTime;
    float4 gUvParameters;
    float4 gMaterialColor;
    float4 gMaterialParameters;
    float4 gTerrainParameters;
};

Texture2D gTerrainColor : register(t0);
Texture2D gUnusedNormal : register(t1);
Texture2D gHeightMap : register(t2);
SamplerState gSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD;
};

static const float CheckerCellsPerAxis = 32.0f;
static const float CheckerDrop = 12.0f;

float CheckerHeightOffset(float2 uv)
{
    const int2 cell = (int2)floor(uv * CheckerCellsPerAxis);
    const int parity = abs(cell.x + cell.y) & 1;
    return parity != 0 ? -CheckerDrop : 0.0f;
}

float4 VSMain(VSInput input) : SV_POSITION
{
    const float2 atlasUv = input.uv * gUvParameters.xy + gUvParameters.zw;
    float3 localPosition = input.position;
    localPosition.y = gHeightMap.SampleLevel(gSampler, saturate(atlasUv), 0).r * gTerrainParameters.x
        + gTerrainParameters.w + CheckerHeightOffset(atlasUv);
    if (input.position.y < -0.5f)
        localPosition.y -= gTerrainParameters.z;
    return mul(mul(float4(localPosition, 1), gWorld), gViewProjection);
}
