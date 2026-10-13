#include "MeshScene.hlsli"
[numthreads(1, 1, 1)] void CSMain() {
    for (uint bin = 0; bin < 2; ++bin)
    {
        uint count = meshArguments.Load(bin * 32), width = min(count, dispatchBase);
        meshArguments.Store3(bin * 32 + 16,
                             uint3(width, count == 0 ? 1 : (count + dispatchBase - 1) / dispatchBase, 1));
    }
}
