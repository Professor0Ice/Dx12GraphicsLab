#include "Lighting.hlsl"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

float SampleLuminance(float2 uv)
{
    float3 albedo = gAlbedo.SampleLevel(gSampler, saturate(uv), 0.0f).rgb;
    return dot(albedo, float3(0.2126f, 0.7152f, 0.0722f));
}

float GreenStencilNearby(float2 pixelPosition)
{
    uint width = 0;
    uint height = 0;
    gStencil.GetDimensions(width, height);
    const int2 center = int2(pixelPosition);
    const int2 maximum = int2(width, height) - 1;
    uint greenMarker = 0;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int2 coordinate = clamp(center + int2(x, y), int2(0, 0), maximum);
            greenMarker = max(greenMarker, gStencil.Load(int3(coordinate, 0)));
        }
    }

    return greenMarker != 0u ? 1.0f : 0.0f;
}

float SobelEdge(float2 uv)
{
    const float2 texel = gPostProcessParameters.yz;
    const float topLeft = SampleLuminance(uv + texel * float2(-1.0f, -1.0f));
    const float top = SampleLuminance(uv + texel * float2( 0.0f, -1.0f));
    const float topRight = SampleLuminance(uv + texel * float2( 1.0f, -1.0f));
    const float left = SampleLuminance(uv + texel * float2(-1.0f,  0.0f));
    const float right = SampleLuminance(uv + texel * float2( 1.0f,  0.0f));
    const float bottomLeft = SampleLuminance(uv + texel * float2(-1.0f,  1.0f));
    const float bottom = SampleLuminance(uv + texel * float2( 0.0f,  1.0f));
    const float bottomRight = SampleLuminance(uv + texel * float2( 1.0f,  1.0f));

    const float gradientX = -topLeft - 2.0f * left - bottomLeft + topRight + 2.0f * right + bottomRight;
    const float gradientY = -topLeft - 2.0f * top - topRight + bottomLeft + 2.0f * bottom + bottomRight;
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
        float greenStencil = 0.0f;
        [branch]
        if (edge > 0.01f)
            greenStencil = GreenStencilNearby(input.position.xy);
        const float3 outlineColor = lerp(0.0f.xxx, float3(0.0f, 0.0f, 1.0f), greenStencil);
        color = lerp(color, outlineColor, edge);
    }
    return float4(color, 1.0f);
}
