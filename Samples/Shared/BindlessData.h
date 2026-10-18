#pragma once
#include "Application.h"
#include "Workshop.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <meshoptimizer.h>
#include <nlohmann/json.hpp>
#include <numeric>
#include <set>
#include <span>

namespace tutorial
{
struct alignas(16) GalleryVertex
{
    DirectX::XMFLOAT4 positionU, normalV, tangent;
};
struct alignas(16) GalleryGeometry
{
    DirectX::XMUINT4 offsets, lengths, resources, extra;
};
struct alignas(16) GalleryMaterial
{
    DirectX::XMFLOAT4 base, factors, emission;
    DirectX::XMUINT4 images, layers;
    DirectX::XMFLOAT4 alpha;
};
struct alignas(16) GalleryInstance
{
    DirectX::XMFLOAT4X4 world, normal;
    DirectX::XMUINT4 ids;
};
struct alignas(16) GalleryMeshlet
{
    DirectX::XMUINT4 offsets;
    DirectX::XMFLOAT4 sphere, cone, apex;
};
struct GalleryConstants
{
    DirectX::XMFLOAT4X4 viewProjection, inverseViewProjection;
    DirectX::XMFLOAT4 camera, sunDirection, sunColor, screen, planes[6];
    DirectX::XMUINT4 controls, heaps;
};
static_assert(sizeof(GalleryVertex) == 48 && sizeof(GalleryGeometry) == 64 && sizeof(GalleryMaterial) == 96);
static_assert(sizeof(GalleryInstance) == 144 && sizeof(GalleryMeshlet) == 64 && sizeof(GalleryConstants) == 320);

struct GalleryData
{
    Workshop source;
    std::vector<GalleryVertex> vertices;
    std::vector<uint32_t> indices, meshletVertices, meshletIndices;
    std::vector<GalleryMeshlet> meshlets;
    std::vector<GalleryGeometry> geometries;
    std::vector<GalleryMaterial> materials;
    std::vector<GalleryInstance> instances;
    std::vector<DirectX::XMUINT2> candidates;
    std::vector<WorkshopImage> images;
    std::array<std::vector<uint32_t>, 4> imageGroups;
    std::string preset, assetHash;
    uint32_t objects = 0, materialCount = 0;
};

inline GalleryData LoadGallery(const Options &o)
{
    using namespace DirectX;
    GalleryData result;
    auto settings = o.scene.empty() ? AssetPath(L"Assets/Scenes/BindlessGallery/BindlessGallery.json") : o.scene;
    auto bytes = ReadBinary(settings);
    auto config = nlohmann::json::parse(bytes);
    result.source = LoadWorkshop(settings);
    result.preset = std::filesystem::path(o.galleryPreset).string();
    const auto &preset = config.at("gallery").at("presets").at(result.preset);
    result.objects = o.galleryObjects ? o.galleryObjects : preset.at("objects").get<uint32_t>();
    result.materialCount = o.galleryMaterials ? o.galleryMaterials : preset.at("materials").get<uint32_t>();
    if (result.objects > 4096 || !result.objects || result.materialCount > 256 || !result.materialCount)
        throw std::runtime_error("Gallery object/material capacity exceeded");
    const auto &mapping = config.at("gallery").at("primitiveGeometry");
    if (mapping.size() != result.source.primitives.size())
        throw std::runtime_error("Gallery geometry mapping size");
    std::array<int, 64> first;
    first.fill(-1);
    for (size_t i = 0; i < mapping.size(); ++i)
    {
        auto g = mapping[i].get<uint32_t>();
        if (g >= 64)
            throw std::runtime_error("Gallery geometry ID out of range");
        if (first[g] < 0)
            first[g] = int(i);
    }
    for (uint32_t g = 0; g < 64; ++g)
    {
        // A fixed-slot SRV may expose a window of the combined buffer. Vulkan
        // storage-buffer descriptor offsets require alignment (at most 256).
        while (result.vertices.size() % 16)
            result.vertices.push_back({});
        while (result.indices.size() % 64)
            result.indices.push_back(0);
        while (result.meshlets.size() % 4)
            result.meshlets.push_back({});
        while (result.meshletVertices.size() % 64)
            result.meshletVertices.push_back(0);
        while (result.meshletIndices.size() % 64)
            result.meshletIndices.push_back(0);
        if (first[g] < 0)
            throw std::runtime_error("Missing Gallery geometry");
        const auto &p = result.source.primitives[first[g]];
        GalleryGeometry geometry{
            {uint32_t(result.vertices.size()), uint32_t(result.indices.size()), uint32_t(result.meshlets.size()), 0},
            {uint32_t(p.vertices.size()), uint32_t(p.indices.size()), 0, 0},
            {3 + g * 5, 4 + g * 5, 5 + g * 5, 6 + g * 5},
            {7 + g * 5, 0, 0, 0}};
        for (const auto &v : p.vertices)
            result.vertices.push_back({{v.position.x, v.position.y, v.position.z, v.uv.x},
                                       {v.normal.x, v.normal.y, v.normal.z, v.uv.y},
                                       v.tangent});
        result.indices.insert(result.indices.end(), p.indices.begin(), p.indices.end());
        size_t bound = meshopt_buildMeshletsBound(p.indices.size(), 64, 96);
        std::vector<meshopt_Meshlet> meshlets(bound);
        std::vector<unsigned> vertices(p.indices.size());
        std::vector<unsigned char> triangles(p.indices.size());
        size_t count = meshopt_buildMeshlets(meshlets.data(), vertices.data(), triangles.data(), p.indices.data(),
                                             p.indices.size(), &p.vertices[0].position.x, p.vertices.size(),
                                             sizeof(PbrVertex), 64, 96, .25f);
        uint32_t vertexBase = uint32_t(result.meshletVertices.size()),
                 triangleBase = uint32_t(result.meshletIndices.size());
        geometry.lengths.z = vertexBase;
        geometry.lengths.w = triangleBase;
        for (size_t j = 0; j < count; ++j)
        {
            const auto &m = meshlets[j];
            auto b = meshopt_computeMeshletBounds(vertices.data() + m.vertex_offset,
                                                  triangles.data() + m.triangle_offset, m.triangle_count,
                                                  &p.vertices[0].position.x, p.vertices.size(), sizeof(PbrVertex));
            result.meshlets.push_back(
                {{uint32_t(result.meshletVertices.size()) - vertexBase,
                  uint32_t(result.meshletIndices.size()) - triangleBase, m.vertex_count, m.triangle_count},
                 {b.center[0], b.center[1], b.center[2], b.radius},
                 {b.cone_axis[0], b.cone_axis[1], b.cone_axis[2], b.cone_cutoff},
                 {b.cone_apex[0], b.cone_apex[1], b.cone_apex[2], 0}});
            result.meshletVertices.insert(result.meshletVertices.end(), vertices.begin() + m.vertex_offset,
                                          vertices.begin() + m.vertex_offset + m.vertex_count);
            for (uint32_t t = 0; t < m.triangle_count * 3; ++t)
                result.meshletIndices.push_back(triangles[m.triangle_offset + t]);
        }
        geometry.offsets.w = uint32_t(count);
        result.geometries.push_back(geometry);
    }
    std::map<uint32_t, uint32_t> imageMap;
    for (uint32_t i = 0; i < result.materialCount; ++i)
    {
        const auto &m = result.source.materials.at(i);
        GalleryMaterial gpu{m.base, m.factors, m.emission,
                            {},     {},        {m.cutoff, float(m.alpha == AlphaMode::Mask), float(m.doubleSided), 0}};
        auto indices = &gpu.images.x, layers = &gpu.layers.x;
        for (uint32_t c = 0; c < 4; ++c)
        {
            auto [it, added] = imageMap.emplace(m.images[c], uint32_t(result.images.size()));
            if (added)
                result.images.push_back(result.source.images.at(m.images[c]));
            indices[c] = it->second;
            auto &group = result.imageGroups[c];
            auto found = std::find(group.begin(), group.end(), it->second);
            if (found == group.end())
            {
                layers[c] = uint32_t(group.size());
                group.push_back(it->second);
            }
            else
                layers[c] = uint32_t(found - group.begin());
        }
        result.materials.push_back(gpu);
    }
    // Use saved placement, independent of requested material budget. No geometry is copied.
    const auto &scene = *std::find_if(result.source.scenes.begin(), result.source.scenes.end(), [&](const auto &s) {
        return s.name == (result.objects > 512 ? "Stress" : result.objects > 128 ? "Standard" : "Mini");
    });
    for (uint32_t i = 0; i < result.objects; ++i)
    {
        const auto &inst = scene.instances.at(i);
        uint32_t g = mapping.at(inst.primitive), material = i % result.materialCount;
        GalleryInstance gpu{inst.world, {}, {g, material, i, 0}};
        XMStoreFloat4x4(&gpu.normal, XMMatrixTranspose(XMMatrixInverse(nullptr, XMLoadFloat4x4(&gpu.world))));
        result.instances.push_back(gpu);
        for (uint32_t m = 0; m < result.geometries[g].offsets.w; ++m)
            result.candidates.push_back({i, m});
    }
    // Stable identical ordering for the three direct submission modes.
    std::stable_sort(result.instances.begin(), result.instances.end(), [](const auto &a, const auto &b) {
        return std::pair(a.ids.y, a.ids.x) < std::pair(b.ids.y, b.ids.x);
    });
    result.candidates.clear();
    if (!o.meshTestScene.empty())
    {
        if (o.meshTestScene != L"offscreen")
            throw std::runtime_error("Gallery test scene: offscreen");
        for (auto &instance : result.instances)
            instance.world._41 += 10000;
    }
    for (uint32_t i = 0; i < result.instances.size(); ++i)
        for (uint32_t m = 0; m < result.geometries[result.instances[i].ids.x].offsets.w; ++m)
            result.candidates.push_back({i, m});
    auto manifest = nlohmann::json::parse(ReadBinary(settings.parent_path() / L"Manifest.json"));
    result.assetHash = manifest.at("assetSha256");
    return result;
}

inline GalleryConstants MakeGalleryConstants(const GalleryData &d, const WorkshopCamera &cam, uint32_t width,
                                             uint32_t height, uint32_t view, bool shadows, bool reflections,
                                             bool culling)
{
    using namespace DirectX;
    GalleryConstants c{};
    auto vp = XMMatrixLookAtLH(XMLoadFloat3(&cam.position), XMLoadFloat3(&cam.target), XMVectorSet(0, 1, 0, 0)) *
              XMMatrixPerspectiveFovLH(XMConvertToRadians(cam.fov), float(width) / height, cam.nearPlane, cam.farPlane);
    XMStoreFloat4x4(&c.viewProjection, vp);
    XMStoreFloat4x4(&c.inverseViewProjection, XMMatrixInverse(nullptr, vp));
    c.camera = {cam.position.x, cam.position.y, cam.position.z, 1};
    c.sunDirection = {.3f, -.85f, .4f, 0};
    c.sunColor = {2, 1.9f, 1.7f, 1};
    c.screen = {float(width), float(height), .001f, 1};
    c.controls = {uint32_t(d.instances.size()), view, uint32_t(shadows), uint32_t(reflections)};
    c.heaps.w = uint32_t(culling);
    auto m = XMMatrixTranspose(vp);
    XMVECTOR planes[] = {m.r[3] + m.r[0], m.r[3] - m.r[0], m.r[3] + m.r[1], m.r[3] - m.r[1], m.r[2], m.r[3] - m.r[2]};
    for (int i = 0; i < 6; ++i)
        XMStoreFloat4(&c.planes[i], XMPlaneNormalize(planes[i]));
    return c;
}

template <class T> inline std::span<const uint8_t> GalleryBytes(const std::vector<T> &v)
{
    return {reinterpret_cast<const uint8_t *>(v.data()), v.size() * sizeof(T)};
}
inline double GalleryMs(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
inline Image LoadGalleryImage(const WorkshopImage &source)
{
    auto image = LoadWicImage(source.path);
    if (image.width != 512 || image.height != 512)
        throw std::runtime_error("Gallery material images must be 512x512");
    return image;
}
inline std::array<uint32_t, 2> GalleryFrustumCandidates(const GalleryData &data, const GalleryConstants &frame)
{
    using namespace DirectX;
    uint32_t count = 0;
    std::set<uint32_t> instances;
    for (auto candidate : data.candidates)
    {
        const auto &instance = data.instances[candidate.x];
        const auto &geometry = data.geometries[instance.ids.x];
        const auto &meshlet = data.meshlets[geometry.offsets.z + candidate.y];
        XMFLOAT3 center;
        XMStoreFloat3(&center, XMVector3TransformCoord(XMLoadFloat4(&meshlet.sphere), XMLoadFloat4x4(&instance.world)));
        const auto &m = instance.world;
        float radius = meshlet.sphere.w * std::sqrt(m._11 * m._11 + m._12 * m._12 + m._13 * m._13);
        bool visible = true;
        if (frame.heaps.w)
            for (auto plane : frame.planes)
                if (plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w < -radius - .0001f)
                    visible = false;
        if (visible)
        {
            ++count;
            instances.insert(candidate.x);
        }
    }
    return {uint32_t(instances.size()), count};
}
} // namespace tutorial
