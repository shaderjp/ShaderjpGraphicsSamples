#include "MeshScene.hlsli"
[numthreads(64, 1, 1)] void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= dispatchBase) return;
    MeshInstance instance = meshInstances[dispatchInstance];
    skinCache[instance.primitive.y + id.x] = SkinVertexAt(id.x, instance.primitive.z);
}
