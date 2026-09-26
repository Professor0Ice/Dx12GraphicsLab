#include "Lighting.hlsl"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

float3 SampleAlbedo(float2 uv)
{
    return gAlbedo.SampleLevel(gSampler, saturate(uv), 0.0f).rgb;
}

float AlbedoLuminance(float3 albedo)
{
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
    const float3 topLeftColor = SampleAlbedo(uv + texel * float2(-1.0f, -1.0f));
    const float3 topColor = SampleAlbedo(uv + texel * float2( 0.0f, -1.0f));
    const float3 topRightColor = SampleAlbedo(uv + texel * float2( 1.0f, -1.0f));
    const float3 leftColor = SampleAlbedo(uv + texel * float2(-1.0f,  0.0f));
    const float3 rightColor = SampleAlbedo(uv + texel * float2( 1.0f,  0.0f));
    const float3 bottomLeftColor = SampleAlbedo(uv + texel * float2(-1.0f,  1.0f));
    const float3 bottomColor = SampleAlbedo(uv + texel * float2( 0.0f,  1.0f));
    const float3 bottomRightColor = SampleAlbedo(uv + texel * float2( 1.0f,  1.0f));

    const float topLeft = AlbedoLuminance(topLeftColor);
    const float top = AlbedoLuminance(topColor);
    const float topRight = AlbedoLuminance(topRightColor);
    const float left = AlbedoLuminance(leftColor);
    const float right = AlbedoLuminance(rightColor);
    const float bottomLeft = AlbedoLuminance(bottomLeftColor);
    const float bottom = AlbedoLuminance(bottomColor);
    const float bottomRight = AlbedoLuminance(bottomRightColor);

    const float gradientX = -topLeft - 2.0f * left - bottomLeft +topRight + 2.0f * right + bottomRight;
    const float gradientY = -topLeft - 2.0f * top - topRight +bottomLeft + 2.0f * bottom + bottomRight;
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
