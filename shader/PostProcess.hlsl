#include "Lighting.hlsl"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

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
    float3 color = ToneMapAndGamma(CalculateSceneColor(input.uv));
    const uint mode = (uint)round(gPostProcessParameters.x);
    if (mode == 1u)
    {
        const float grayscale = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        color = grayscale.xxx;
    }
    else if (mode == 2u)
    {
        const float edge = SobelEdge(input.uv);
        color = lerp(color, float3(0.01f, 0.015f, 0.025f), edge);
    }
    return float4(color, 1.0f);
}
