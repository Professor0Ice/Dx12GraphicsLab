struct Particle
{
    float3 position;
    float padding0;
    float3 velocity;
    float padding1;
};

struct ParticleSortItem
{
    float distanceSquared;
    uint particleIndex;
};

cbuffer ParticleSortConstants : register(b0)
{
    float3 gCameraPosition;
    uint gParticleCount;
    uint gStageK;
    uint gStageJ;
    float2 gPadding;
};

StructuredBuffer<Particle> gParticles : register(t0);
RWStructuredBuffer<ParticleSortItem> gSortItems : register(u0);

[numthreads(64, 1, 1)]
void CSBuildKeys(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= gParticleCount)
        return;

    const float3 offset = gParticles[index].position - gCameraPosition;
    ParticleSortItem item;
    item.distanceSquared = dot(offset, offset);
    item.particleIndex = index;
    gSortItems[index] = item;
}

[numthreads(64, 1, 1)]
void CSBitonicStep(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= gParticleCount)
        return;

    const uint partner = index ^ gStageJ;
    if (partner <= index || partner >= gParticleCount)
        return;

    const ParticleSortItem first = gSortItems[index];
    const ParticleSortItem second = gSortItems[partner];
    const bool ascending = (index & gStageK) == 0;
    const bool firstAfterSecond = first.distanceSquared > second.distanceSquared ||(first.distanceSquared == second.distanceSquared && first.particleIndex > second.particleIndex);
    const bool swapItems = ascending ? firstAfterSecond : !firstAfterSecond;

    if (swapItems)
    {
        gSortItems[index] = second;
        gSortItems[partner] = first;
    }
}
