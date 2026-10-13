#pragma once
#include "DeferredData.h"
#include <cstddef>
#include <meshoptimizer.h>
namespace tutorial
{
inline constexpr uint32_t MeshletVertices = 64, MeshletTriangles = 96, MeshTextureCount = 64;
struct alignas(16) MeshVertex
{
    DirectX::XMFLOAT4 positionU, normalV, tangent;
};
struct alignas(16) Meshlet
{
    DirectX::XMUINT4 offsets;
    DirectX::XMFLOAT4 sphere, cone, apex;
};
struct alignas(16) MeshPrimitive
{
    DirectX::XMUINT4 offsets;
    DirectX::XMUINT4 meshlets;
};
struct alignas(16) MeshMaterial
{
    DirectX::XMFLOAT4 base, factors, emission;
    DirectX::XMUINT4 images;
    DirectX::XMFLOAT4 alpha;
};
struct alignas(16) MeshInstance
{
    DirectX::XMFLOAT4X4 world, normal;
    DirectX::XMUINT4 primitive;
};
struct alignas(16) MeshFrame
{
    DirectX::XMFLOAT4 planes[6], view;
    DirectX::XMUINT4 controls;
};
static_assert(sizeof(MeshVertex) == 48 && sizeof(Meshlet) == 64 && sizeof(MeshPrimitive) == 32);
static_assert(sizeof(MeshMaterial) == 80 && sizeof(MeshInstance) == 144 && sizeof(MeshFrame) == 128);
static_assert(offsetof(Meshlet, sphere) == 16 && offsetof(MeshMaterial, images) == 48 &&
              offsetof(MeshInstance, primitive) == 128);
struct MeshletData
{
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices, meshletVertices, meshletIndices;
    std::vector<Meshlet> meshlets;
    std::vector<MeshPrimitive> primitives;
    std::vector<MeshMaterial> materials;
};
inline MeshletData BuildMeshletData(const Workshop &w, bool basic = false)
{
    if (w.images.empty() || w.images.size() > MeshTextureCount)
        throw std::runtime_error("Mesh Shader requires 1..64 textures");
    MeshletData d;
    for (const auto &p : w.primitives)
    {
        MeshPrimitive primitive{
            {uint32_t(d.vertices.size()), uint32_t(d.indices.size()), p.material, uint32_t(p.indices.size())},
            {uint32_t(d.meshlets.size()), 0, 0, 0}};
        for (const auto &v : p.vertices)
            d.vertices.push_back({{v.position.x, v.position.y, v.position.z, v.uv.x},
                                  {v.normal.x, v.normal.y, v.normal.z, v.uv.y},
                                  v.tangent});
        d.indices.insert(d.indices.end(), p.indices.begin(), p.indices.end());
        if (!basic)
        {
            size_t bound = meshopt_buildMeshletsBound(p.indices.size(), MeshletVertices, MeshletTriangles);
            std::vector<meshopt_Meshlet> result(bound);
            std::vector<unsigned> vertices(p.indices.size());
            std::vector<unsigned char> triangles(p.indices.size());
            size_t count = meshopt_buildMeshlets(result.data(), vertices.data(), triangles.data(), p.indices.data(),
                                                 p.indices.size(), &p.vertices[0].position.x, p.vertices.size(),
                                                 sizeof(PbrVertex), MeshletVertices, MeshletTriangles, .25f);
            for (size_t i = 0; i < count; ++i)
            {
                const auto &m = result[i];
                auto bounds = meshopt_computeMeshletBounds(
                    vertices.data() + m.vertex_offset, triangles.data() + m.triangle_offset, m.triangle_count,
                    &p.vertices[0].position.x, p.vertices.size(), sizeof(PbrVertex));
                Meshlet gpu{{uint32_t(d.meshletVertices.size()), uint32_t(d.meshletIndices.size()), m.vertex_count,
                             m.triangle_count},
                            {bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius},
                            {bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2], bounds.cone_cutoff},
                            {bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2], 0}};
                d.meshletVertices.insert(d.meshletVertices.end(), vertices.begin() + m.vertex_offset,
                                         vertices.begin() + m.vertex_offset + m.vertex_count);
                for (uint32_t t = 0; t < m.triangle_count * 3; ++t)
                    d.meshletIndices.push_back(triangles[m.triangle_offset + t]);
                d.meshlets.push_back(gpu);
            }
            primitive.meshlets.y = uint32_t(count);
        }
        d.primitives.push_back(primitive);
    }
    for (const auto &m : w.materials)
        d.materials.push_back({m.base,
                               m.factors,
                               m.emission,
                               {m.images[0], m.images[1], m.images[2], m.images[3]},
                               {m.cutoff, float(m.alpha == AlphaMode::Mask), float(m.doubleSided), 0}});
    return d;
}
inline std::vector<MeshInstance> MakeMeshInstances(const Workshop &w, int scene, float occluder)
{
    using namespace DirectX;
    std::vector<MeshInstance> result;
    for (const auto &i : w.scenes.at(scene).instances)
    {
        auto world = XMLoadFloat4x4(&i.world);
        if (i.movable)
            world *= XMMatrixTranslation(occluder, 0, 0);
        MeshInstance gpu{};
        XMStoreFloat4x4(&gpu.world, world);
        XMStoreFloat4x4(&gpu.normal, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
        gpu.primitive = {i.primitive, 0, 0, 0};
        result.push_back(gpu);
    }
    return result;
}
inline std::vector<DirectX::XMUINT4> MakeMeshCandidates(const Workshop &w, const MeshletData &d, int scene, int preset)
{
    std::vector<DirectX::XMUINT4> result;
    const auto &s = w.scenes.at(scene);
    for (uint32_t i = 0; i < s.instances.size(); ++i)
    {
        const auto &p = d.primitives.at(s.instances[i].primitive);
        const auto &m = w.materials.at(p.offsets.z);
        if (m.alpha == AlphaMode::Blend || (m.alpha == AlphaMode::Mask && preset == 0))
            continue;
        for (uint32_t j = 0; j < p.meshlets.y; ++j)
            result.push_back({i, p.meshlets.x + j, uint32_t(m.doubleSided), 0});
    }
    return result;
}
inline MeshFrame MakeMeshFrame(const DeferredConstants &c, bool shadow, uint32_t count, uint32_t culling, bool view,
                               bool stats)
{
    using namespace DirectX;
    auto matrix = shadow ? c.lightViewProjection : c.viewProjection;
    const auto &m = matrix;
    XMFLOAT4 columns[] = {{m._11, m._21, m._31, m._41},
                          {m._12, m._22, m._32, m._42},
                          {m._13, m._23, m._33, m._43},
                          {m._14, m._24, m._34, m._44}};
    MeshFrame f{};
    const XMVECTOR x = XMLoadFloat4(&columns[0]), y = XMLoadFloat4(&columns[1]), z = XMLoadFloat4(&columns[2]),
                   w = XMLoadFloat4(&columns[3]);
    XMVECTOR planes[] = {w + x, w - x, w + y, w - y, z, w - z};
    for (int i = 0; i < 6; ++i)
        XMStoreFloat4(&f.planes[i], XMPlaneNormalize(planes[i]));
    f.view = shadow ? c.sunDirection : c.camera;
    f.view.w = shadow ? 1.f : 0.f;
    f.controls = {count, culling, uint32_t(view), uint32_t(stats)};
    return f;
}
} // namespace tutorial
