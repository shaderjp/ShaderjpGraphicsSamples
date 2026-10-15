// Shared linear blend deformation for VS, CS and MS. Row vectors, LH, metres.
struct SkinInfluence { uint4 joints; float4 weights; };
struct SkinBone { row_major float4x4 matrix; };
VK_BINDING(38, 0) StructuredBuffer<SkinInfluence> skinInfluences : register(t96);
VK_BINDING(39, 0) StructuredBuffer<SkinBone> skinBones : register(t97);
#if SKIN_TECHNIQUE >= 4
VK_BINDING(40, 0) StructuredBuffer<float4> skinDualQuaternionParts : register(t98);
#else
VK_BINDING(40, 0) StructuredBuffer<float4> skinBounds : register(t98);
#endif
#if SKIN_TECHNIQUE == 6
VK_BINDING(42, 0) StructuredBuffer<float4> skinBounds : register(t99);
#endif
VK_BINDING(41, 0) RWStructuredBuffer<MeshVertex> skinCache : register(u7);
#if SKIN_TECHNIQUE >= 4
#include "DualQuaternion.hlsli"
#endif
MeshVertex SkinVertexAt(uint index, uint paletteBase)
{
    MeshVertex v = meshVertices[index];
    SkinInfluence i = skinInfluences[index];
#if SKIN_TECHNIQUE >= 4
    if ((meshMode & 0x10000) != 0) return SkinVertexDqs(v, i, paletteBase);
#endif
    row_major float4x4 blend = skinBones[paletteBase + i.joints.x].matrix * i.weights.x
        + skinBones[paletteBase + i.joints.y].matrix * i.weights.y
        + skinBones[paletteBase + i.joints.z].matrix * i.weights.z
        + skinBones[paletteBase + i.joints.w].matrix * i.weights.w;
    v.positionU.xyz = mul(float4(v.positionU.xyz, 1), blend).xyz;
    v.normalV.xyz = SafeNormalize(mul(v.normalV.xyz, (float3x3)blend));
    float3 tangent = mul(v.tangent.xyz, (float3x3)blend);
    v.tangent.xyz = SafeNormalize(tangent - v.normalV.xyz * dot(v.normalV.xyz, tangent));
    return v;
}
