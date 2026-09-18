
cbuffer ObjectConstants : register(b0)
{
    row_major float4x4 gWorld;
    row_major float4x4 gLightViewProjection;
    float4 gCameraAndTime;
    float4 gUvParameters;
    float4 gMaterialColor;
    float4 gTessellationParameters;
};

Texture2D gAlbedo : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDisplacement : register(t2); 
SamplerState gSampler : register(s0);

struct ControlPoint
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD;
};

ControlPoint VSMain(ControlPoint input)
{
    return input;
}

struct PatchConstants
{
    float edges[4] : SV_TessFactor;
    float inside[2] : SV_InsideTessFactor;
};

PatchConstants PatchConstantFunction(InputPatch<ControlPoint, 4> patch, uint patchId : SV_PrimitiveID)
{
    PatchConstants output;
    // Средняя точка patch используется для оценки расстояния до камеры.
    float3 center = (patch[0].position + patch[1].position + patch[2].position + patch[3].position) * 0.25f;
    float3 worldCenter = mul(float4(center, 1.0f), gWorld).xyz;
    float distanceToCamera = distance(worldCenter, gCameraAndTime.xyz);
    float range = max(gTessellationParameters.w - gTessellationParameters.z, 0.001f);

    float factor = lerp(gTessellationParameters.x, gTessellationParameters.y,
                        saturate((distanceToCamera - gTessellationParameters.z) / range));
    [unroll] for (int i = 0; i < 4; ++i) output.edges[i] = factor;
    output.inside[0] = factor;
    output.inside[1] = factor;
    return output;
}

[domain("quad")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
[patchconstantfunc("PatchConstantFunction")]
ControlPoint HSMain(InputPatch<ControlPoint, 4> patch, uint pointId : SV_OutputControlPointID,uint patchId : SV_PrimitiveID)
{
    return patch[pointId];
}

[domain("quad")]
float4 DSMain(PatchConstants constants, float2 domain : SV_DomainLocation,
              const OutputPatch<ControlPoint, 4> patch) : SV_POSITION
{
    // Двойной lerp билинейно восстанавливает позицию, нормаль и UV внутри quad patch.
    float3 p0 = lerp(patch[0].position, patch[1].position, domain.y);
    float3 p1 = lerp(patch[3].position, patch[2].position, domain.y);
    float3 position = lerp(p0, p1, domain.x);
    float3 n0 = lerp(patch[0].normal, patch[1].normal, domain.y);
    float3 n1 = lerp(patch[3].normal, patch[2].normal, domain.y);
    float3 normal = normalize(lerp(n0, n1, domain.x));
    float2 uv0 = lerp(patch[0].uv, patch[1].uv, domain.y);
    float2 uv1 = lerp(patch[3].uv, patch[2].uv, domain.y);
    float2 uv = lerp(uv0, uv1, domain.x) * gUvParameters.xy + gCameraAndTime.w * gUvParameters.zw;

    position += normal * ((gDisplacement.SampleLevel(gSampler, uv, 0).r - 0.5f) * 1.25f);
    float4 worldPosition = mul(float4(position, 1.0f), gWorld);
    return mul(worldPosition, gLightViewProjection);
}
