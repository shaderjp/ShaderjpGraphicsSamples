#ifndef MESH_SCENE_INCLUDED
#define MESH_SCENE_INCLUDED
#include "../Deferred/Lighting.hlsli"
struct MeshVertex
{
    float4 positionU, normalV, tangent;
};
struct Meshlet
{
    uint4 offsets;
    float4 sphere, cone, apex;
};
struct MeshPrimitive
{
    uint4 offsets, meshlets;
};
struct MeshMaterial
{
    float4 base, factors, emission;
    uint4 images;
    float4 alpha;
};
struct MeshInstance
{
    row_major float4x4 world, normal;
    uint4 primitive;
};
struct MeshFrame
{
    float4 planes[6], view;
    uint4 controls;
};
VK_BINDING(24, 0) StructuredBuffer<MeshFrame> meshFrames : register(t20);
VK_BINDING(25, 0) StructuredBuffer<MeshVertex> meshVertices : register(t21);
VK_BINDING(26, 0) StructuredBuffer<uint> originalIndices : register(t22);
VK_BINDING(27, 0) StructuredBuffer<Meshlet> meshlets : register(t23);
VK_BINDING(28, 0) StructuredBuffer<uint> meshletVertices : register(t24);
VK_BINDING(29, 0) StructuredBuffer<uint> meshletIndices : register(t25);
VK_BINDING(30, 0) StructuredBuffer<MeshPrimitive> meshPrimitives : register(t26);
VK_BINDING(31, 0) StructuredBuffer<MeshMaterial> meshMaterials : register(t27);
VK_BINDING(32, 0) StructuredBuffer<MeshInstance> meshInstances : register(t28);
VK_BINDING(33, 0) StructuredBuffer<uint4> meshCandidates : register(t29);
VK_BINDING(34, 0) RWStructuredBuffer<uint2> visibleMeshlets : register(u5);
VK_BINDING(35, 0) RWByteAddressBuffer meshArguments : register(u6);
VK_BINDING(36, 0) Texture2D<float4> meshTextures[64] : register(t32);
VK_BINDING(37, 0) StructuredBuffer<uint> meshReadArguments : register(t30);
#ifdef __spirv__
struct MeshDispatchData
{
    uint instanceIndex, baseIndex, passIndex, mode;
};
[[vk::push_constant]] ConstantBuffer<MeshDispatchData> meshDispatch;
#define dispatchInstance meshDispatch.instanceIndex
#define dispatchBase meshDispatch.baseIndex
#define meshPass meshDispatch.passIndex
#define meshMode meshDispatch.mode
#else
cbuffer MeshDispatch : register(b2)
{
    uint dispatchInstance, dispatchBase, meshPass, meshMode;
};
#endif
struct MeshOutput
{
    float4 position : SV_Position;
    VK_LOCATION(0) float2 uv : TEXCOORD0;
    VK_LOCATION(1) float3 worldPosition : TEXCOORD1;
    VK_LOCATION(2) float3 normal : TEXCOORD2;
    VK_LOCATION(3) float4 tangent : TEXCOORD3;
    VK_LOCATION(4) nointerpolation uint materialId : TEXCOORD4;
    VK_LOCATION(5) nointerpolation uint meshletId : TEXCOORD5;
    VK_LOCATION(6) float4 debugWeights : TEXCOORD6;
};
#include "SkinningCommon.hlsli"
MeshOutput TransformCachedVertex(uint instance, MeshVertex vertex, uint meshletId, uint sourceIndex)
{
    MeshInstance object = meshInstances[instance];
    MeshPrimitive primitive = meshPrimitives[object.primitive.x];
    MeshOutput o;
    float4 p = mul(float4(vertex.positionU.xyz, 1), object.world);
    o.position = mul(p, meshPass == 0 ? lightViewProjection : viewProjection);
    o.worldPosition = p.xyz;
    o.uv = float2(vertex.positionU.w, vertex.normalV.w);
    o.normal = mul(float4(vertex.normalV.xyz, 0), object.normal).xyz;
    o.tangent = float4(mul(float4(vertex.tangent.xyz, 0), object.world).xyz, vertex.tangent.w);
    o.materialId = primitive.offsets.z;
    o.meshletId = meshletId;
    o.debugWeights = skinInfluences[sourceIndex].weights;
    return o;
}
MeshOutput TransformMeshVertex(uint instance, uint index, uint meshletId)
{
    MeshInstance object = meshInstances[instance];
    MeshPrimitive primitive = meshPrimitives[object.primitive.x];
    uint flatIndex = primitive.offsets.x + index;
    MeshVertex vertex = meshVertices[flatIndex];
    if (object.primitive.y != 0xffffffff) vertex = SkinVertexAt(flatIndex, object.primitive.z);
    return TransformCachedVertex(instance, vertex, meshletId, flatIndex);
}
Surface ReadMeshMaterial(MeshOutput i, bool front)
{
    MeshMaterial m = meshMaterials[i.materialId];
    Surface s;
    float4 b = meshTextures[NonUniformResourceIndex(m.images.x)].Sample(materialSampler, i.uv) * m.base;
    if (m.alpha.y != 0)
        clip(b.a - m.alpha.x);
    s.base = b.rgb;
    s.alpha = b.a;
    float3 orm = meshTextures[NonUniformResourceIndex(m.images.z)].Sample(materialSampler, i.uv).rgb;
    s.ao = lerp(1, orm.r, m.factors.w);
    s.metallic = saturate(orm.b * m.factors.x);
    s.roughness = max(saturate(orm.g * m.factors.y), .045);
    float3 N = SafeNormalize(i.normal), T = SafeNormalize(i.tangent.xyz - N * dot(N, i.tangent.xyz)),
           B = i.tangent.w * cross(N, T);
    float3 nt = meshTextures[NonUniformResourceIndex(m.images.y)].Sample(materialSampler, i.uv).xyz * 2 - 1;
    nt.xy *= m.factors.z;
    s.normal = SafeNormalize(T * nt.x + B * nt.y + N * nt.z) * (front ? 1 : -1);
    s.emission = meshTextures[NonUniformResourceIndex(m.images.w)].Sample(materialSampler, i.uv).rgb * m.emission.rgb;
    return s;
}
float3 MeshletColor(uint id)
{
    uint h = id * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28) + 4)) ^ h) * 277803737u;
    h = (h >> 22) ^ h;
    return .2 + .8 * float3(h & 255, (h >> 8) & 255, (h >> 16) & 255) / 255;
}
bool VisibleMeshlet(uint instance, uint id)
{
    MeshFrame frame = meshFrames[meshPass == 0 ? 0 : 1];
    if (frame.controls.y == 0)
        return true;
    MeshInstance object = meshInstances[instance];
    Meshlet m = meshlets[id];
    float4 sphere = object.primitive.w == 0xffffffff ? m.sphere : skinBounds[object.primitive.w + id];
    float3 center = mul(float4(sphere.xyz, 1), object.world).xyz;
    float scale = length(object.world[0].xyz), radius = sphere.w * scale;
    for (uint p = 0; p < 6; ++p)
        if (dot(frame.planes[p], float4(center, 1)) < -radius - .0001)
            return false;
    MeshPrimitive primitive = meshPrimitives[object.primitive.x];
    if (false && frame.controls.y == 2 && meshMaterials[primitive.offsets.z].alpha.z == 0 && m.cone.w < 1)
    {
        float3 axis = SafeNormalize(mul(float4(m.cone.xyz, 0), object.world).xyz);
        float3 apex = mul(float4(m.apex.xyz, 1), object.world).xyz;
        float3 direction = frame.view.w != 0 ? -frame.view.xyz : SafeNormalize(apex - frame.view.xyz);
        if (dot(direction, axis) >= m.cone.w + .0001)
            return false;
    }
    return true;
}
#endif
