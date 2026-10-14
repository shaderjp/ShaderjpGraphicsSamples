#include "MeshScene.hlsli"
float4 PSMain(MeshOutput i, bool front : SV_IsFrontFace) : SV_Target
{
    Surface s = ReadMeshMaterial(i, front);
    if (controls.y == 1)
        return float4(s.base, 1);
    if (controls.y == 2) return float4(s.normal * .5 + .5, 1);
    if (controls.y == 3) return float4(s.metallic.xxx, 1);
    if (controls.y == 4) return float4(s.roughness.xxx, 1);
    if (controls.y == 5) return float4(s.emission, 1);
    if (controls.y == 6) return float4(i.position.zzz, 1);
    if (controls.y == 7) return float4(shadowTexture.Load(int3(clamp(uint2(i.position.xy / screen.xy * shadow.w), 0, uint(shadow.w)-1),0)).xxx, 1);
    if (controls.y == 8) return float4(i.debugWeights.y, i.debugWeights.x, i.debugWeights.z + i.debugWeights.w, 1);
    return float4(meshFrames[1].controls.z != 0 ? MeshletColor(i.meshletId) : Shade(s, i.worldPosition), s.alpha);
}
