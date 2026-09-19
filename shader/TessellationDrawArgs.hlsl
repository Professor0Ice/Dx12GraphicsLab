ByteAddressBuffer gFilledSize : register(t0);
RWByteAddressBuffer gDrawArgs : register(u0);

[numthreads(1, 1, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
    // Stream Output выдаёт размер в байтах; DrawInstanced нужен счётчик вершин.
    gDrawArgs.Store(0, gFilledSize.Load(0) / 44);
    gDrawArgs.Store(4, 1);
    gDrawArgs.Store(8, 0);
    gDrawArgs.Store(12, 0);
}
