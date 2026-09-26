struct Particle
{
    float3 position;
    float padding0;
    float3 velocity;
    float padding1;
};

cbuffer SimulationConstants : register(b0)
{
    float gDeltaTime;
    float gTotalTime;
    uint gParticleCount;
    float gPadding;
    float3 gEmitterPosition;
    float gFallSpeed;
};

ConsumeStructuredBuffer<Particle> gParticlesIn : register(u0);
AppendStructuredBuffer<Particle> gParticlesOut : register(u1);

float Hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return (value & 0x00ffffffu) / 16777216.0f;
}

Particle SpawnParticle(uint id)
{
    const uint generation = (uint)(gTotalTime * 1000.0f);

    Particle particle;
    particle.position = gEmitterPosition + float3(
        (Hash(id * 2u + generation) - 0.5f) * 14.0f,
        0.0f,
        (Hash(id * 2u + generation + 1u) - 0.5f) * 20.0f);
    particle.padding0 = 0.0f;
    particle.velocity = float3(0.0f, -gFallSpeed, 0.0f);
    particle.padding1 = 0.0f;
    return particle;
}

Particle SpawnReverseParticle(uint id)
{
    const uint generation = (uint)(gTotalTime * 1000.0f);

    Particle particle;
    particle.position = float3(
        gEmitterPosition.x + (Hash(id * 2u + generation) - 0.5f) * 14.0f,
        0.15f,
        gEmitterPosition.z + (Hash(id * 2u + generation + 1u) - 0.5f) * 20.0f);
    particle.padding0 = 0.0f;
    particle.velocity = float3(0.0f, gFallSpeed, 0.0f);
    particle.padding1 = 0.0f;
    return particle;
}

[numthreads(64, 1, 1)]
void CSInitialize(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gParticleCount)
        return;

    Particle particle = SpawnParticle(dispatchThreadId.x);
    particle.position.y = 0.5f + Hash(dispatchThreadId.x * 3u + 2u) *
                          (gEmitterPosition.y - 0.5f);
    gParticlesOut.Append(particle);
}

[numthreads(64, 1, 1)]
void CSInitializeReverse(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gParticleCount)
        return;

    Particle particle = SpawnReverseParticle(dispatchThreadId.x);
    particle.position.y = 0.5f + Hash(dispatchThreadId.x * 3u + 2u) *
                          (gEmitterPosition.y - 0.5f);
    gParticlesOut.Append(particle);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gParticleCount)
        return;

    Particle particle = gParticlesIn.Consume();
    if (particle.position.y <= 0.02f)
        particle = SpawnParticle(dispatchThreadId.x);
    else
        particle.position += particle.velocity * gDeltaTime;

    gParticlesOut.Append(particle);
}

[numthreads(64, 1, 1)]
void CSReverse(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gParticleCount)
        return;

    Particle particle = gParticlesIn.Consume();
    if (particle.position.y >= gEmitterPosition.y)
        particle = SpawnReverseParticle(dispatchThreadId.x);
    else
        particle.position += particle.velocity * gDeltaTime;

    gParticlesOut.Append(particle);
}
