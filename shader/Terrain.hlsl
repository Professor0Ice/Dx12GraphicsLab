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

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION0;
    float3 normal : NORMAL;
    float2 atlasUv : TEXCOORD;
    float height : TEXCOORD1;
};

float SampleHeight(float2 uv)
{
    return gHeightMap.SampleLevel(gSampler, saturate(uv), 0).r;
}

static const float CheckerCellsPerAxis = 32.0f;
static const float CheckerDrop = 12.0f;

float CheckerHeightOffset(float2 uv)
{
    const int2 cell = (int2)floor(uv * CheckerCellsPerAxis);
    const int parity = abs(cell.x + cell.y) & 1;
    return parity != 0 ? -CheckerDrop : 0.0f;
}

float SampleTerrainWorldHeight(float2 uv)
{
    return SampleHeight(uv) * gTerrainParameters.x + gTerrainParameters.w + CheckerHeightOffset(uv);
}

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    const float2 atlasUv = input.uv * gUvParameters.xy + gUvParameters.zw;
    uint width, height;
    gHeightMap.GetDimensions(width, height);
    const float2 texel = 1.0f / float2(width, height);

    const float h = SampleHeight(atlasUv);
    const float checkerOffset = CheckerHeightOffset(atlasUv);
    const float hL = SampleTerrainWorldHeight(atlasUv - float2(texel.x, 0));
    const float hR = SampleTerrainWorldHeight(atlasUv + float2(texel.x, 0));
    const float hD = SampleTerrainWorldHeight(atlasUv - float2(0, texel.y));
    const float hU = SampleTerrainWorldHeight(atlasUv + float2(0, texel.y));
    const float sampleSpan = 2.0f * gTerrainParameters.y / max((float)width - 1.0f, 1.0f);
    const float3 localNormal = normalize(float3(hL - hR, sampleSpan, hD - hU));

    float3 localPosition = input.position;
    localPosition.y = h * gTerrainParameters.x + gTerrainParameters.w + checkerOffset;
    if (input.position.y < -0.5f)
        localPosition.y -= gTerrainParameters.z;

    const float4 worldPosition = mul(float4(localPosition, 1), gWorld);
    output.position = mul(worldPosition, gViewProjection);
    output.worldPosition = worldPosition.xyz;
    output.normal = localNormal;
    output.atlasUv = atlasUv;
    output.height = h + checkerOffset / max(gTerrainParameters.x, 0.0001f);
    return output;
}

struct GBufferOutput
{
    float4 albedo : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 position : SV_TARGET2;
};

GBufferOutput PSMain(VSOutput input)
{
    GBufferOutput output;
    const float slope = 1.0f - saturate(input.normal.y);
    float3 color = gTerrainColor.Sample(gSampler, input.atlasUv).rgb;
    const float3 rock = float3(0.25f, 0.23f, 0.21f);
    const float3 snow = float3(0.72f, 0.76f, 0.78f);
    color = lerp(color, rock, smoothstep(0.22f, 0.65f, slope));
    color = lerp(color, snow, smoothstep(0.76f, 0.94f, input.height) * (1.0f - slope));
    output.albedo = float4(color * gMaterialColor.rgb, 1);
    output.normal = float4(normalize(input.normal), 0.82f);
    output.position = float4(input.worldPosition, 0.0f);
    return output;
}
