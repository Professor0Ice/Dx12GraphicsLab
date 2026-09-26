static const float PI = 3.14159265359f;

struct Light
{
    float4 positionAndRange;
    float4 directionAndType;
    float4 colorAndIntensity;
    float4 spotAngles;
    float4 padding;
};

cbuffer LightingConstants : register(b0)
{
    row_major float4x4 gInverseViewProjection;
    row_major float4x4 gShadowViewProjections[4];
    float4 gCameraAndLightCount;
    // x: highest mip of the prefiltered environment, y: IBL intensity, z: exposure.
    float4 gIblParameters;
    float4 gCascadeSplits;
    float4 gShadowParameters;
    float4 gCameraForward;
    float4 gPostProcessParameters;
    Light gLights[16];
};

Texture2D gAlbedo : register(t0);
Texture2D gNormal : register(t1);       // xyz: world normal, w: perceptual roughness
Texture2D gPosition : register(t2);     // xyz: world position, w: metallic
Texture2DArray gShadowMap : register(t3);
TextureCube gIrradianceMap : register(t4);
TextureCube gPrefilteredEnvironmentMap : register(t5);
Texture2D gBrdfIntegrationMap : register(t6);
SamplerState gSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1);
SamplerState gIblSampler : register(s2);

float DistributionGGX(float3 normal, float3 halfway, float roughness)
{
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float nDotH = saturate(dot(normal, halfway));
    const float denominator = nDotH * nDotH * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / max(PI * denominator * denominator, 1e-5f);
}

float GeometrySchlickGGX(float nDotDirection, float roughness)
{
    const float r = roughness + 1.0f;
    const float k = r * r * 0.125f;
    return nDotDirection / max(nDotDirection * (1.0f - k) + k, 1e-5f);
}

float GeometrySmith(float3 normal, float3 viewDirection, float3 lightDirection, float roughness)
{
    return GeometrySchlickGGX(saturate(dot(normal, viewDirection)), roughness) *
           GeometrySchlickGGX(saturate(dot(normal, lightDirection)), roughness);
}

float3 FresnelSchlick(float cosine, float3 f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - saturate(cosine), 5.0f);
}

float3 FresnelSchlickRoughness(float cosine, float3 f0, float roughness)
{
    return f0 + (max((1.0f - roughness).xxx, f0) - f0) *
                pow(1.0f - saturate(cosine), 5.0f);
}

float3 EvaluatePbrLight(Light light, float3 worldPosition, float3 normal,
                        float3 viewDirection, float3 albedo, float metallic, float roughness)
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
        lightDirection = toLight / max(distanceToLight, 1e-4f);
        const float normalizedDistance = distanceToLight / max(light.positionAndRange.w, 1e-3f);
        const float rangeFalloff = saturate(1.0f - pow(normalizedDistance, 4.0f));
        attenuation = rangeFalloff * rangeFalloff / max(distanceToLight * distanceToLight, 0.25f);

        if (type == 2)
        {
            const float cone = dot(-lightDirection, normalize(light.directionAndType.xyz));
            attenuation *= smoothstep(light.spotAngles.y, light.spotAngles.x, cone);
        }
    }

    const float nDotL = saturate(dot(normal, lightDirection));
    float3 contribution = 0.0f;
    if (nDotL > 0.0f)
    {
        const float3 halfway = normalize(viewDirection + lightDirection);
        const float3 f0 = lerp(0.04f.xxx, albedo, metallic);
        const float distribution = DistributionGGX(normal, halfway, roughness);
        const float geometry = GeometrySmith(normal, viewDirection, lightDirection, roughness);
        const float3 fresnel = FresnelSchlick(saturate(dot(halfway, viewDirection)), f0);
        const float3 specular = distribution * geometry * fresnel /
                                max(4.0f * saturate(dot(normal, viewDirection)) * nDotL, 1e-4f);
        const float3 diffuseWeight = (1.0f - fresnel) * (1.0f - metallic);
        const float3 radiance = light.colorAndIntensity.rgb * light.colorAndIntensity.w * attenuation;
        contribution = (diffuseWeight * albedo / PI + specular) * radiance * nDotL;
    }
    return contribution;
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
    float visibility = 1.0f;
    if (shadowPosition.z > 0.0f && shadowPosition.z < 1.0f &&
        all(uv >= 0.0f) && all(uv <= 1.0f))
    {
        const float3 directionToLight = normalize(-gLights[0].directionAndType.xyz);
        const float normalSlope = 1.0f - saturate(dot(normal, directionToLight));
        const float bias = max(gShadowParameters.y, gShadowParameters.z * normalSlope);
        visibility = 0.0f;
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
        visibility /= 9.0f;
    }
    return visibility;
}

float3 ReconstructWorldDirection(float2 uv)
{
    const float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 1.0f, 1.0f);
    const float4 world = mul(clip, gInverseViewProjection);
    return normalize(world.xyz / max(abs(world.w), 1e-5f) - gCameraAndLightCount.xyz);
}

float3 EvaluateIbl(float3 normal, float3 viewDirection, float3 albedo,
                   float metallic, float roughness, float ao)
{
    const float nDotV = saturate(dot(normal, viewDirection));
    const float3 f0 = lerp(0.04f.xxx, albedo, metallic);
    const float3 fresnel = FresnelSchlickRoughness(nDotV, f0, roughness);
    const float3 diffuseWeight = (1.0f - fresnel) * (1.0f - metallic);

    const float3 irradiance = gIrradianceMap.SampleLevel(gIblSampler, normal, 0.0f).rgb;
    const float3 diffuse = irradiance * albedo;
    const float3 reflection = reflect(-viewDirection, normal);
    const float3 prefiltered = gPrefilteredEnvironmentMap.SampleLevel(
        gIblSampler, reflection, roughness * gIblParameters.x).rgb;
    const float2 brdf = gBrdfIntegrationMap.SampleLevel(
        gIblSampler, float2(nDotV, roughness), 0.0f).rg;
    const float3 specular = prefiltered * (fresnel * brdf.x + brdf.y);
    return (diffuseWeight * diffuse + specular) * ao * gIblParameters.y;
}

float3 CalculateSceneColor(float2 uv)
{
    const float4 albedoSample = gAlbedo.SampleLevel(gSampler, uv, 0.0f);
    const float4 normalSample = gNormal.SampleLevel(gSampler, uv, 0.0f);
    const float4 positionSample = gPosition.SampleLevel(gSampler, uv, 0.0f);

    float3 color = 0.0f;
    if (dot(normalSample.xyz, normalSample.xyz) < 0.01f)
    {
        const float3 viewRay = ReconstructWorldDirection(uv);
        color = gPrefilteredEnvironmentMap.SampleLevel(gIblSampler, viewRay, 0.0f).rgb *
                gIblParameters.y;
    }
    else
    {
        const float3 albedo = pow(saturate(albedoSample.rgb), 2.2f);
        const float3 normal = normalize(normalSample.xyz);
        const float3 viewDirection = normalize(gCameraAndLightCount.xyz - positionSample.xyz);
        const float roughness = clamp(normalSample.w, 0.045f, 1.0f);
        const float metallic = saturate(positionSample.w);
        color = EvaluateIbl(normal, viewDirection, albedo, metallic, roughness,
                            saturate(albedoSample.a));

        const uint lightCount = min((uint)gCameraAndLightCount.w, 16u);
        for (uint i = 0; i < lightCount; ++i)
        {
            const float shadow = i == 0 ? EvaluateDirectionalShadow(positionSample.xyz, normal) : 1.0f;
            color += shadow * EvaluatePbrLight(gLights[i], positionSample.xyz, normal,
                                               viewDirection, albedo, metallic, roughness);
        }
    }
    return color;
}

float3 ToneMapAndGamma(float3 color)
{
    color *= gIblParameters.z;
    color = saturate((color * (2.51f * color + 0.03f)) /
                     (color * (2.43f * color + 0.59f) + 0.14f));
    return pow(color, 1.0f / 2.2f);
}
