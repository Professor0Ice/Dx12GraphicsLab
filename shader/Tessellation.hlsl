
cbuffer ObjectConstants : register(b0)
{
    row_major float4x4 gWorld;
    row_major float4x4 gViewProjection;
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
    // Оцениваем расстояние от центра patch до камеры в мировом пространстве.
    float3 center = (patch[0].position + patch[1].position + patch[2].position + patch[3].position) * 0.25f;
    float3 worldCenter = mul(float4(center, 1.0f), gWorld).xyz;
    float distanceToCamera = distance(worldCenter, gCameraAndTime.xyz);
    float range = max(gTessellationParameters.w - gTessellationParameters.z, 0.001f);
    float alpha = saturate((distanceToCamera - gTessellationParameters.z) / range);
    
    float factor = lerp(gTessellationParameters.x, gTessellationParameters.y, alpha);
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
ControlPoint HSMain(InputPatch<ControlPoint, 4> patch, uint pointId : SV_OutputControlPointID, uint patchId : SV_PrimitiveID)
{
    return patch[pointId];
}

struct DomainOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float2 uv : TEXCOORD;
};

ControlPoint InterpolateQuad(const OutputPatch<ControlPoint, 4> patch, float2 uv)
{
    ControlPoint result;
    float3 p0 = lerp(patch[0].position, patch[1].position, uv.y);
    float3 p1 = lerp(patch[3].position, patch[2].position, uv.y);
    result.position = lerp(p0, p1, uv.x);
    result.normal = normalize(lerp(lerp(patch[0].normal, patch[1].normal, uv.y),
                                   lerp(patch[3].normal, patch[2].normal, uv.y), uv.x));
    result.tangent = lerp(lerp(patch[0].tangent, patch[1].tangent, uv.y),
                          lerp(patch[3].tangent, patch[2].tangent, uv.y), uv.x);
    result.uv = lerp(lerp(patch[0].uv, patch[1].uv, uv.y),
                     lerp(patch[3].uv, patch[2].uv, uv.y), uv.x);
    return result;
}

[domain("quad")]
DomainOutput DSMain(PatchConstants constants, float2 domain : SV_DomainLocation, const OutputPatch<ControlPoint, 4> patch)
{
    ControlPoint control = InterpolateQuad(patch, domain);
    float2 animatedUv = control.uv * gUvParameters.xy + gCameraAndTime.w * gUvParameters.zw;
    float displacement = (gDisplacement.SampleLevel(gSampler, animatedUv, 0).r - 0.5f) * 1.25f;
    control.position += control.normal * displacement; // Карта высот меняет настоящую геометрию.

    DomainOutput output;
    float4 worldPosition = mul(float4(control.position, 1), gWorld);
    output.position = mul(worldPosition, gViewProjection); // Итоговая clip-space позиция.
    output.worldPosition = worldPosition.xyz;
    output.normal = normalize(mul(float4(control.normal, 0), gWorld).xyz);
    output.tangent = normalize(mul(float4(control.tangent.xyz, 0), gWorld).xyz);
    output.uv = animatedUv;
    return output;
}

// Записываем треугольники после tessellator/domain shader в Stream Output.
[maxvertexcount(3)]
void GSCache(triangle DomainOutput input[3], inout TriangleStream<DomainOutput> output)
{
    [unroll] for (uint i = 0; i < 3; ++i)
        output.Append(input[i]);
}

struct CachedInput
{
    float3 worldPosition : POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float2 uv : TEXCOORD;
};

DomainOutput CachedVS(CachedInput input)
{
    DomainOutput output;
    output.position = mul(float4(input.worldPosition, 1.0f), gViewProjection);
    output.worldPosition = input.worldPosition;
    output.normal = input.normal;
    output.tangent = input.tangent;
    output.uv = input.uv;
    return output;
}

struct GBufferOutput
{
    float4 albedo : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 position : SV_TARGET2;
};

GBufferOutput PSMain(DomainOutput input)
{
    GBufferOutput output;
    // Из normal и tangent строится TBN-базис для перевода normal map в мир.
    float3 n = normalize(input.normal);
    float3 t = normalize(input.tangent - n * dot(input.tangent, n));
    float3 b = normalize(cross(n, t));
    float3 mapNormal = gNormalMap.Sample(gSampler, input.uv).xyz * 2.0f - 1.0f;
    float3 worldNormal = normalize(mapNormal.x * t + mapNormal.y * b + mapNormal.z * n);
    output.albedo = gAlbedo.Sample(gSampler, input.uv) * gMaterialColor; // Цвет.
    output.normal = float4(worldNormal, 48.0f); // Нормаль и более резкий specular.
    output.position = float4(input.worldPosition, 0.55f); // Позиция и сила specular.
    return output;
}
