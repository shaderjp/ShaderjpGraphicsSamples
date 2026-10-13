#include "Scene.hlsli"
[numthreads(32, 1, 1)][outputtopology("triangle")] void MSMain(uint3 group : SV_GroupID, uint thread : SV_GroupIndex,
                                                               out vertices Output output[96],
                                                               out indices uint3 triangles[32]) {
    uint begin = (group.x + counts.y) * 32, num = min(32, counts.x / 3 - begin);
    SetMeshOutputCounts(num * 3, num);
    if (thread < num)
    {
        for (uint v = 0; v < 3; ++v)
            output[thread * 3 + v] = Transform(indices[(begin + thread) * 3 + v]);
        triangles[thread] = uint3(thread * 3, thread * 3 + 1, thread * 3 + 2);
    }
}
