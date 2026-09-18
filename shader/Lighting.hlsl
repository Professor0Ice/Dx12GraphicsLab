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
    row_major float4x4 gInverseViewProjection; // Обратная матрица камеры
    row_major float4x4 gShadowViewProjections[4]; // Матрица каждого каскада
    float4 gCameraAndLightCount;               
    float4 gAmbient;                         
    float4 gCascadeSplits; //глубина каскадов
    float4 gShadowParameters;                      
    float4 gCameraForward;                       
    Light gLights[16];                          
};

Texture2D gAlbedo : register(t0);           
Texture2D gNormal : register(t1);         
Texture2D gPosition : register(t2);        
Texture2DArray gShadowMap : register(t3);  
SamplerState gSampler : register(s0);  
SamplerComparisonState gShadowSampler : register(s1); 

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = uv;
    output.position = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    return output;
}

float3 EvaluateLight(Light light, float3 worldPosition, float3 normal, float3 viewDirection,
                     float3 albedo, float shininess, float specularStrength)
{
    const int type = (int)round(light.directionAndType.w);
    float3 lightDirection;
    float attenuation = 1.0f;
    if (type == 0)
    {
        // Солнце
        lightDirection = normalize(-light.directionAndType.xyz);
    }
    else
    {
        // point/spot
        float3 toLight = light.positionAndRange.xyz - worldPosition;
        float distanceToLight = length(toLight);
        lightDirection = toLight / max(distanceToLight, 0.0001f);
        float normalizedDistance = saturate(distanceToLight / max(light.positionAndRange.w, 0.001f));
        attenuation = (1.0f - normalizedDistance) * (1.0f - normalizedDistance); // Плавное квадратичное затухание.
        if (type == 2)
        {
            // Проверка находится ли в конусе
            float cone = dot(-lightDirection, normalize(light.directionAndType.xyz));
            attenuation *= smoothstep(light.spotAngles.y, light.spotAngles.x, cone);
        }
    }
    float diffuse = saturate(dot(normal, lightDirection)); 
    float3 reflected = reflect(-lightDirection, normal); // Отражённый луч дл блика.
    float specular = pow(saturate(dot(reflected, viewDirection)), shininess) * specularStrength;
    return (albedo * diffuse + specular.xxx) * light.colorAndIntensity.rgb *
           light.colorAndIntensity.w * attenuation;
}

float EvaluateDirectionalShadow(float3 worldPosition, float3 normal)
{
    // Проекция точки на направление камеры даёт глубину для выбора каскада.
    float viewDepth = dot(worldPosition - gCameraAndLightCount.xyz, gCameraForward.xyz);
    uint cascade = viewDepth > gCascadeSplits.x ? 1u : 0u;
    cascade = viewDepth > gCascadeSplits.y ? 2u : cascade;
    cascade = viewDepth > gCascadeSplits.z ? 3u : cascade;

    float4 shadowPosition = mul(float4(worldPosition, 1.0f), gShadowViewProjections[cascade]);
    shadowPosition.xyz /= shadowPosition.w;
    float2 uv = float2(shadowPosition.x * 0.5f + 0.5f,
                       -shadowPosition.y * 0.5f + 0.5f);
    
    
    if (shadowPosition.z <= 0.0f) return 1.0f;
    if (shadowPosition.z >= 1.0f) return 1.0f;
    if (any(uv < 0.0f)) return 1.0f;
    if (any(uv > 1.0f)) return 1.0f;

    float3 directionToLight = normalize(-gLights[0].directionAndType.xyz);
    float normalSlope = 1.0f - saturate(dot(normal, directionToLight));
    float bias = max(gShadowParameters.y, gShadowParameters.z * normalSlope);
    float visibility = 0.0f;
    
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * gShadowParameters.x;
            visibility += gShadowMap.SampleCmpLevelZero(
                gShadowSampler, float3(uv + offset, cascade), shadowPosition.z - bias);
        }
    }
    return visibility / 9.0f;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float4 albedoSample = gAlbedo.Sample(gSampler, input.uv);
    float4 normalSample = gNormal.Sample(gSampler, input.uv);
    float4 positionSample = gPosition.Sample(gSampler, input.uv);

    if (dot(normalSample.xyz, normalSample.xyz) < 0.01f)
        return float4(albedoSample.rgb, 1.0f);

    float3 normal = normalize(normalSample.xyz);
    float3 viewDirection = normalize(gCameraAndLightCount.xyz - positionSample.xyz);
    float3 color = albedoSample.rgb * gAmbient.rgb; 
    const uint lightCount = min((uint)gCameraAndLightCount.w, 16u);
    for (uint i = 0; i < lightCount; ++i)
    {

        float shadow = i == 0 ? EvaluateDirectionalShadow(positionSample.xyz, normal) : 1.0f;
        color += shadow * EvaluateLight(gLights[i], positionSample.xyz, normal, viewDirection,
                                        albedoSample.rgb, normalSample.w, positionSample.w);
    }

    color = color / (1.0f + color); 
    color = pow(saturate(color), 1.0f / 2.2f); 
    return float4(color, 1.0f);
}
