#include "MeshScene.hlsli"
struct MeshPayload
{
    uint ids[32];
};
[numthreads(64, 1, 1)][outputtopology("triangle")] void MSMain(uint3 group : SV_GroupID, uint thread : SV_GroupIndex,
#ifdef TASK_INPUT
                                                               in payload MeshPayload payload,
#endif
                                                               out vertices MeshOutput vertices[64],
                                                               out indices uint3 triangles[96]) {
    uint instance = dispatchInstance, id = dispatchBase + group.x;
    bool emit = true;
#ifdef TASK_INPUT
    id = payload.ids[group.x];
#else
    if ((meshMode & 255) == 3)
    {
        uint bin = meshMode >> 8, workIndex = group.x + group.y * dispatchBase;
        uint count = meshReadArguments[bin * 8];
        emit = workIndex < count;
        if (emit)
        {
            uint2 visible = visibleMeshlets[bin * meshFrames[1].controls.x + workIndex];
            instance = visible.x;
            id = visible.y;
        }
        else
        {
            id = 0;
            instance = 0;
        }
    }
#endif
    Meshlet m = meshlets[id];
    SetMeshOutputCounts(emit ? m.offsets.z : 0, emit ? m.offsets.w : 0);
    if (!emit)
        return;
    if (thread < m.offsets.z)
        vertices[thread] = TransformMeshVertex(instance, meshletVertices[m.offsets.x + thread], id);
    for (uint t = thread; t < m.offsets.w; t += 64)
        triangles[t] = uint3(meshletIndices[m.offsets.y + t * 3], meshletIndices[m.offsets.y + t * 3 + 1],
                             meshletIndices[m.offsets.y + t * 3 + 2]);
}
