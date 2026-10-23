static const float PlanetRadius = 6371.0f;
static const float AtmosphereRadius = 6451.0f;
static const float AtmosphereWorldScale = 0.1f;

float2 AtmosphereIntersection(float3 origin, float3 direction, float radius)
{
    const float b = dot(origin, direction);
    const float c = dot(origin, origin) - radius * radius;
    const float discriminant = b * b - c;
    const float root = sqrt(max(discriminant, 0.0f));
    return discriminant < 0.0f ? float2(-1.0f, -1.0f) : float2(-b - root, -b + root);
}

float2 AtmosphereDensity(float3 position)
{
    const float altitude = max(length(position) - PlanetRadius, 0.0f);
    return exp(-altitude / float2(8.0f, 1.2f));
}

float3 ApplyAtmosphere(float3 sceneColor, float3 viewRay, float distanceToSurface)
{
    float3 origin = gCameraAndLightCount.xyz * AtmosphereWorldScale;
    origin.y = max(origin.y, 0.001f) + PlanetRadius;
    const float2 atmosphereHit = AtmosphereIntersection(origin, viewRay, AtmosphereRadius);
    const float start = max(atmosphereHit.x, 0.0f);
    float end = min(atmosphereHit.y, distanceToSurface);
    const float2 groundHit = AtmosphereIntersection(origin, viewRay, PlanetRadius);
    if (groundHit.x > 0.0f)
        end = min(end, groundHit.x);
    if (end > start)
    {
        const float3 rayleigh = float3(0.0058f, 0.0135f, 0.0331f) * gAtmosphereParameters.x;
        const float mie = lerp(0.002f, 0.12f, gAtmosphereParameters.y) * gAtmosphereParameters.x;
        const float3 sunDirection = normalize(-gLights[0].directionAndType.xyz);
        const float cosine = clamp(dot(viewRay, sunDirection), -1.0f, 1.0f);
        const float rayleighPhase = 3.0f * (1.0f + cosine * cosine) / (16.0f * PI);
        const float g = 0.76f;
        const float miePhase = 3.0f * (1.0f - g * g) * (1.0f + cosine * cosine) / (8.0f * PI * (2.0f + g * g) * pow(max(1.0f + g * g - 2.0f * g * cosine, 0.001f), 1.5f));
        float2 viewDepth = 0.0f;
        float3 scatteredLight = 0.0f;

        [loop]
        for (int i = 0; i < 12; ++i)
        {
            const float t0 = i / 12.0f;
            const float t1 = (i + 1) / 12.0f;
            const float segmentStart = lerp(start, end, t0 * t0);
            const float segmentEnd = lerp(start, end, t1 * t1);
            const float stepLength = segmentEnd - segmentStart;
            const float3 position = origin + viewRay * ((segmentStart + segmentEnd) * 0.5f);
            const float2 density = AtmosphereDensity(position);
            const float2 segmentDepth = density * stepLength;
            viewDepth += segmentDepth * 0.5f;

            const float2 sunGroundHit = AtmosphereIntersection(position, sunDirection, PlanetRadius);
            if (sunGroundHit.x <= 0.0f)
            {
                const float sunDistance = AtmosphereIntersection(position, sunDirection, AtmosphereRadius).y;
                float2 sunDepth = 0.0f;
                [loop]
                for (int j = 0; j < 6; ++j)
                {
                    const float s0 = j / 6.0f;
                    const float s1 = (j + 1) / 6.0f;
                    const float lightStart = sunDistance * s0 * s0;
                    const float lightEnd = sunDistance * s1 * s1;
                    sunDepth += AtmosphereDensity(position + sunDirection * ((lightStart + lightEnd) * 0.5f)) * (lightEnd - lightStart);
                }

                const float2 opticalDepth = viewDepth + sunDepth;
                const float3 transmittance = exp(-(rayleigh * opticalDepth.x + 1.1f * mie * opticalDepth.y));
                scatteredLight += transmittance * (rayleigh * rayleighPhase * segmentDepth.x + mie * miePhase * segmentDepth.y);
            }
            viewDepth += segmentDepth * 0.5f;
        }

        const float3 transmission = exp(-(rayleigh * viewDepth.x + 1.1f * mie * viewDepth.y));
        const float3 sunlight = gLights[0].colorAndIntensity.rgb * gLights[0].colorAndIntensity.w * 20.0f;
        sceneColor = sceneColor * transmission + scatteredLight * sunlight;
    }
    return sceneColor;
}
