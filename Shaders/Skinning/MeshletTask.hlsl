#include "MeshScene.hlsli"
struct MeshPayload
{
    uint ids[32];
};
groupshared MeshPayload result;
groupshared uint visibleCount;
[numthreads(32, 1, 1)] void ASMain(uint3 group : SV_GroupID, uint thread : SV_GroupIndex) {
    if (thread == 0)
        visibleCount = 0;
    GroupMemoryBarrierWithGroupSync();
    MeshInstance instance = meshInstances[dispatchInstance];
    MeshPrimitive p = meshPrimitives[instance.primitive.x];
    uint local = dispatchBase + group.x * 32 + thread;
    if (local < p.meshlets.y)
    {
        bool visible = VisibleMeshlet(dispatchInstance, p.meshlets.x + local);
        if (meshFrames[meshPass == 0 ? 0 : 1].controls.w != 0)
        {
            uint unused;
            meshArguments.InterlockedAdd(128 + (meshPass == 0 ? 0 : 16), 1, unused);
            if (visible)
                meshArguments.InterlockedAdd(132 + (meshPass == 0 ? 0 : 16), 1, unused);
        }
        if (visible)
        {
            uint slot;
            InterlockedAdd(visibleCount, 1, slot);
            result.ids[slot] = p.meshlets.x + local;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    DispatchMesh(visibleCount, 1, 1, result);
}
