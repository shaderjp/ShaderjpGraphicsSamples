#include "MeshScene.hlsli"
[numthreads(64, 1, 1)] void CSMain(uint id : SV_DispatchThreadID) {
    if (id >= meshFrames[1].controls.x)
        return;
    uint4 candidate = meshCandidates[id];
    if (!VisibleMeshlet(candidate.x, candidate.y))
        return;
    uint slot;
    meshArguments.InterlockedAdd(candidate.z * 32, 1, slot);
    visibleMeshlets[candidate.z * meshFrames[1].controls.x + slot] = candidate.xy;
}
