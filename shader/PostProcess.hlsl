struct Light
{
    float4 positionAndRange;
    float4 directionAndType;
    float4 colorAndIntensity;
    float4 spotAngles;
    float4 padding;
};

cbuffer PostProcessConstants : register(b0)
{
    row_major float4x4 gInverseViewProjection;
    row_major float4x4 gShadowViewProjections[4];
    float4 gCameraAndLightCount;
    float4 gAmbient;
    float4 gCascadeSplits;
    float4 gShadowParameters;
    float4 gCameraForward;
    // x: режим (0 = off, 1 = grayscale, 2 = Sobel), yz: размер одного texel.
    float4 gPostProcessParameters;
    Light gLights[16];
};

// Заготовка post-process pixel shader: все основные каналы G-buffer доступны
// напрямую, поэтому сюда легко добавлять эффекты, использующие материал,
// нормаль или мировую позицию пикселя.
Texture2D gAlbedo : register(t0);
Texture2D gNormal : register(t1);
Texture2D gPosition : register(t2);
Texture2DArray gShadowMap : register(t3);
SamplerState gSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

float3 EvaluateLight(Light light, float3 worldPosition, float3 normal, float3 viewDirection,
                     float3 albedo, float shininess, float specularStrength)
{
    const int type = (int)round(light.directionAndType.w);
    float3 lightDirection;
    float attenuation = 1.0f;

    if (type == 0)
    {
        lightDirection = normalize(-light.directionAndType.xyz);
    }
    else
    {
        const float3 toLight = light.positionAndRange.xyz - worldPosition;
        const float distanceToLight = length(toLight);
        lightDirection = toLight / max(distanceToLight, 0.0001f);
        const float normalizedDistance = saturate(distanceToLight / max(light.positionAndRange.w, 0.001f));
        attenuation = (1.0f - normalizedDistance) * (1.0f - normalizedDistance);

        if (type == 2)
        {
            const float cone = dot(-lightDirection, normalize(light.directionAndType.xyz));
            attenuation *= smoothstep(light.spotAngles.y, light.spotAngles.x, cone);
        }
    }

    const float diffuse = saturate(dot(normal, lightDirection));
    const float3 reflected = reflect(-lightDirection, normal);
    const float specular = pow(saturate(dot(reflected, viewDirection)), shininess) * specularStrength;
    return (albedo * diffuse + specular.xxx) * light.colorAndIntensity.rgb *
           light.colorAndIntensity.w * attenuation;
}

float EvaluateDirectionalShadow(float3 worldPosition, float3 normal)
{
    const float viewDepth = dot(worldPosition - gCameraAndLightCount.xyz, gCameraForward.xyz);
    uint cascade = viewDepth > gCascadeSplits.x ? 1u : 0u;
    cascade = viewDepth > gCascadeSplits.y ? 2u : cascade;
    cascade = viewDepth > gCascadeSplits.z ? 3u : cascade;

    float4 shadowPosition = mul(float4(worldPosition, 1.0f), gShadowViewProjections[cascade]);
    shadowPosition.xyz /= shadowPosition.w;
    const float2 uv = float2(shadowPosition.x * 0.5f + 0.5f,
                             -shadowPosition.y * 0.5f + 0.5f);

    if (shadowPosition.z <= 0.0f || shadowPosition.z >= 1.0f ||
        any(uv < 0.0f) || any(uv > 1.0f))
        return 1.0f;

    const float3 directionToLight = normalize(-gLights[0].directionAndType.xyz);
    const float normalSlope = 1.0f - saturate(dot(normal, directionToLight));
    const float bias = max(gShadowParameters.y, gShadowParameters.z * normalSlope);
    float visibility = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 offset = float2(x, y) * gShadowParameters.x;
            visibility += gShadowMap.SampleCmpLevelZero(
                gShadowSampler, float3(uv + offset, cascade), shadowPosition.z - bias);
        }
    }
    return visibility / 9.0f;
}

float3 CalculateSceneColor(float2 uv)
{
    const float4 albedoSample = gAlbedo.Sample(gSampler, uv);
    const float4 normalSample = gNormal.Sample(gSampler, uv);
    const float4 positionSample = gPosition.Sample(gSampler, uv);

    if (dot(normalSample.xyz, normalSample.xyz) < 0.01f)
        return albedoSample.rgb;

    const float3 normal = normalize(normalSample.xyz);
    const float3 viewDirection = normalize(gCameraAndLightCount.xyz - positionSample.xyz);
    float3 color = albedoSample.rgb * gAmbient.rgb;
    const uint lightCount = min((uint)gCameraAndLightCount.w, 16u);

    for (uint i = 0; i < lightCount; ++i)
    {
        const float shadow = i == 0 ? EvaluateDirectionalShadow(positionSample.xyz, normal) : 1.0f;
        color += shadow * EvaluateLight(gLights[i], positionSample.xyz, normal, viewDirection,
                                        albedoSample.rgb, normalSample.w, positionSample.w);
    }

    color = color / (1.0f + color);
    return pow(saturate(color), 1.0f / 2.2f);
}

float AlbedoLuminance(float2 uv)
{
    const float3 color = gAlbedo.SampleLevel(gSampler, saturate(uv), 0.0f).rgb;
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float SobelEdge(float2 uv)
{
    const float2 texel = gPostProcessParameters.yz;
    const float topLeft     = AlbedoLuminance(uv + texel * float2(-1.0f, -1.0f));
    const float top         = AlbedoLuminance(uv + texel * float2( 0.0f, -1.0f));
    const float topRight    = AlbedoLuminance(uv + texel * float2( 1.0f, -1.0f));
    const float left        = AlbedoLuminance(uv + texel * float2(-1.0f,  0.0f));
    const float right       = AlbedoLuminance(uv + texel * float2( 1.0f,  0.0f));
    const float bottomLeft  = AlbedoLuminance(uv + texel * float2(-1.0f,  1.0f));
    const float bottom      = AlbedoLuminance(uv + texel * float2( 0.0f,  1.0f));
    const float bottomRight = AlbedoLuminance(uv + texel * float2( 1.0f,  1.0f));

    const float gradientX = -topLeft - 2.0f * left - bottomLeft +
                             topRight + 2.0f * right + bottomRight;
    const float gradientY = -topLeft - 2.0f * top - topRight +
                             bottomLeft + 2.0f * bottom + bottomRight;
    return saturate(length(float2(gradientX, gradientY)) * 2.0f);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 color = CalculateSceneColor(input.uv);
    const uint mode = (uint)round(gPostProcessParameters.x);

    if (mode == 1u)
    {
        const float grayscale = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        color = grayscale.xxx;
    }
    else if (mode == 2u)
    {
        // Сохраняем сцену читаемой, затемняем найденные Sobel-фильтром границы.
        const float edge = SobelEdge(input.uv);
        color = lerp(color, float3(0.01f, 0.015f, 0.025f), edge);
    }

    return float4(color, 1.0f);
}
