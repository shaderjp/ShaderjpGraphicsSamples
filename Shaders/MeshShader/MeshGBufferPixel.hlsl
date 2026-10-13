#include "MeshScene.hlsli"
struct Targets
{
    float4 baseAo : SV_Target0;
    float2 normal : SV_Target1;
    float2 metalRough : SV_Target2;
    float3 emission : SV_Target3;
};
Targets PSMain(MeshOutput i, bool front : SV_IsFrontFace)
{
    Surface s = ReadMeshMaterial(i, front);
    Targets o;
    o.baseAo = float4(s.base, s.ao);
    o.normal = EncodeNormal(s.normal);
    o.metalRough = float2(s.metallic, s.roughness);
    o.emission = s.emission;
    if (meshFrames[1].controls.z != 0)
    {
        o.baseAo = float4(0, 0, 0, 1);
        o.emission = MeshletColor(i.meshletId);
    }
    return o;
}
