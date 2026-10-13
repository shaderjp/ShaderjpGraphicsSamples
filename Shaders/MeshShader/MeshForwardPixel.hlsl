#include "MeshScene.hlsli"
float4 PSMain(MeshOutput i, bool front : SV_IsFrontFace) : SV_Target
{
    Surface s = ReadMeshMaterial(i, front);
    if (controls.y == 1)
        return float4(s.base, 1);
    return float4(meshFrames[1].controls.z != 0 ? MeshletColor(i.meshletId) : Shade(s, i.worldPosition), s.alpha);
}
