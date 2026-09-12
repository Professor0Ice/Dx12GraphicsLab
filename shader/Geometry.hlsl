cbuffer ObjectConstants : register(b0)
{
    row_major float4x4 gWorld;
    row_major float4x4 gViewProjection;
    float4 gCameraAndTime;
    float4 gUvParameters;       // tiling.xy, scrolling speed.zw
    float4 gMaterialColor;
    float4 gTessellationParameters;
};

Texture2D gAlbedo : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDisplacement : register(t2);
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
    float3 tangent : TANGENT;
    float2 uv : TEXCOORD;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4 worldPosition = mul(float4(input.position, 1.0f), gWorld);
    output.position = mul(worldPosition, gViewProjection);
    output.worldPosition = worldPosition.xyz;
    output.normal = normalize(mul(float4(input.normal, 0.0f), gWorld).xyz);
    output.tangent = normalize(mul(float4(input.tangent.xyz, 0.0f), gWorld).xyz);
    output.uv = input.uv * gUvParameters.xy + gCameraAndTime.w * gUvParameters.zw;
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
    float3 geometricNormal = normalize(input.normal);
    float3 tangent = normalize(input.tangent - geometricNormal * dot(input.tangent, geometricNormal));
    float3 bitangent = normalize(cross(geometricNormal, tangent));
    float3 tangentNormal = gNormalMap.Sample(gSampler, input.uv).xyz * 2.0f - 1.0f;
    float3 worldNormal = normalize(tangentNormal.x * tangent + tangentNormal.y * bitangent +
                                   tangentNormal.z * geometricNormal);
    output.albedo = gAlbedo.Sample(gSampler, input.uv) * gMaterialColor;
    output.normal = float4(worldNormal, 32.0f);
    output.position = float4(input.worldPosition, 0.42f);
    return output;
}

