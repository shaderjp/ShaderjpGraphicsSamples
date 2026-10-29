#pragma once
#include "RaytracingData.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <meshoptimizer.h>
#include <nlohmann/json.hpp>
#include <set>

namespace tutorial
{
// CPU scene/mesh processing only. Each executable owns every D3D12 object and build.
struct ClusterGeometry
{
    DirectX::XMUINT4 offsets; // first vertex, first uint32 index, triangles, distinct vertex indices
    DirectX::XMUINT4 extra;   // primitive, template, motion; tile index or ribbon height as float bits
};
static_assert(sizeof(ClusterGeometry) == 32);
struct ClusterTopology
{
    uint32_t indexOffset = 0, triangles = 0, uniqueVertices = 0, rate = 0, mask = 0;
};
struct ClusterSceneData
{
    RaySceneData rays;
    std::vector<ClusterGeometry> clusters;
    std::vector<DirectX::XMUINT2> ranges;
    std::vector<ClusterTopology> topologies;
    uint32_t tileFirst = 0, tileCount = 0, tileSide = 0;
};
inline Workshop LoadGeometryLab(const Options &options, int technique,
                                const std::filesystem::path &assetRoot = {})
{
    using namespace DirectX;
    auto asset = [&](const std::filesystem::path &p)
    { return assetRoot.empty() ? AssetPath(p) : assetRoot / p; };
    Workshop w = LoadWorkshop(asset(L"Assets/Scenes/Workshop/Workshop.json"));
    const auto settings =
        options.scene.empty() ? asset(L"Assets/Models/GeometryLab/GeometryLab.json") : options.scene;
    const auto text = ReadBinary(settings);
    const auto config = nlohmann::json::parse(text);
    if (config.at("profile") != "geometry-lab-1" || config.at("vertexStride") != sizeof(PbrVertex))
        throw std::runtime_error("Geometry Lab: invalid profile or vertex stride");
    const auto binary = ReadBinary(settings.parent_path() / config.at("binary").get<std::string>());
    if (binary.size() > 64 * 1024 * 1024)
        throw std::runtime_error("Geometry Lab: binary too large");
    auto sourceMaterial = w.materials.front();
    for (const auto &m : w.materials)
        if (m.id.find("concrete") != std::string::npos || m.id.find("Concrete") != std::string::npos)
            sourceMaterial = m;
    w.materials.clear();
    std::map<std::string, uint32_t> materialIds;
    for (const auto &[name, values] : config.at("materials").items())
    {
        auto m = sourceMaterial;
        m.id = name;
        m.alpha = AlphaMode::Opaque;
        m.doubleSided = true;
        m.base = {values.at(0), values.at(1), values.at(2), 1};
        m.factors = {values.at(3), values.at(4), 0, 0};
        m.emission = {};
        materialIds[name] = uint32_t(w.materials.size());
        w.materials.push_back(m);
    }
    std::map<std::string, WorkshopPrimitive> meshes;
    for (const auto &entry : config.at("primitives"))
    {
        WorkshopPrimitive p;
        p.id = entry.at("name");
        p.material = materialIds.at(entry.at("material"));
        const uint64_t vo = entry.at("vertexOffset"), vc = entry.at("vertexCount"),
                       io = entry.at("indexOffset"), ic = entry.at("indexCount");
        if (!vc || vc > 1000000 || !ic || ic > 6000000 || ic % 3 || vo > binary.size() ||
            io > binary.size() || vc * sizeof(PbrVertex) > binary.size() - vo || ic * 4 > binary.size() - io)
            throw std::runtime_error("Geometry Lab: invalid vertex/index range");
        p.vertices.resize(size_t(vc));
        p.indices.resize(size_t(ic));
        std::memcpy(p.vertices.data(), binary.data() + vo, size_t(vc) * sizeof(PbrVertex));
        std::memcpy(p.indices.data(), binary.data() + io, size_t(ic) * 4);
        for (auto i : p.indices)
            if (i >= vc)
                throw std::runtime_error("Geometry Lab: index out of range");
        for (const auto &v : p.vertices)
            for (float f : {v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z,
                            v.uv.x, v.uv.y})
                if (!std::isfinite(f))
                    throw std::runtime_error("Geometry Lab: non-finite vertex");
        meshes.emplace(p.id, std::move(p));
    }
    w.primitives.clear();
    w.scenes.clear();
    w.cameras.clear();
    w.path.clear();
    for (const auto &name : {"Floor", "Plinth",
                             technique == 21   ? "Ribbon"
                             : technique == 23 ? "Panel"
                                               : "Relief"})
        w.primitives.push_back(meshes.at(name));
    if (technique == 23)
        for (auto &vertex : w.primitives[0].vertices)
        {
            // Cover the full Stress grid without duplicating floor geometry.
            vertex.position.x *= 4;
            vertex.position.z *= 4;
            vertex.uv.x *= 4;
            vertex.uv.y *= 4;
            XMStoreFloat3(&vertex.normal,
                          XMVector3Normalize(XMVectorSet(vertex.normal.x * .25f, vertex.normal.y,
                                                         vertex.normal.z * .25f, 0)));
            auto tangent = XMVector3Normalize(
                XMVectorSet(vertex.tangent.x * 4, vertex.tangent.y, vertex.tangent.z * 4, 0));
            XMFLOAT3 t;
            XMStoreFloat3(&t, tangent);
            vertex.tangent = {t.x, t.y, t.z, vertex.tangent.w};
        }
    for (int stress = 0; stress < 2; ++stress)
    {
        WorkshopScene scene;
        scene.name = stress ? "Stress" : "Standard";
        scene.minimum = {-64, -4, -64};
        scene.maximum = {64, 12, 64};
        scene.direction = {-.35f, .85f, .4f};
        scene.sun = {1, .88f, .7f, 2.3f};
        auto add = [&](uint32_t primitive, float x, float y, float z)
        {
            WorkshopInstance inst;
            inst.id = "geometry:" + std::to_string(scene.instances.size());
            inst.primitive = primitive;
            XMStoreFloat4x4(&inst.world, XMMatrixTranslation(x, y, z));
            scene.instances.push_back(inst);
        };
        add(0, 0, 0, 0);
        if (technique == 23)
        {
            const int side = stress ? 128 : 64;
            for (int z = 0; z < side; ++z)
                for (int x = 0; x < side; ++x)
                    add(2, (x - (side - 1) * .5f) * .38f, 0, (z - (side - 1) * .5f) * .85f);
        }
        else if (technique != 22)
        {
            int side = stress ? 5 : 1;
            for (int z = 0; z < side; ++z)
                for (int x = 0; x < side; ++x)
                {
                    float a = (x - (side - 1) * .5f) * 8, b = (z - (side - 1) * .5f) * 7;
                    add(1, a, 0, b);
                    add(2, a, 0, b);
                }
        }
        if (technique != 23 && options.clusterTest.empty())
        {
            // Reuse the stone plinth mesh for the gallery's columns and lintel.
            for (float z : {-7.f, 0.f, 7.f})
                for (float x : {-7.f, 7.f})
                {
                    add(1, x, 0, z);
                    XMStoreFloat4x4(&scene.instances.back().world,
                                    XMMatrixScaling(.12f, 6, .12f) * XMMatrixTranslation(x, 0, z));
                }
            add(1, 0, 3.7f, -7);
            XMStoreFloat4x4(&scene.instances.back().world,
                            XMMatrixScaling(3.2f, .15f, .12f) * XMMatrixTranslation(0, 3.7f, -7));
        }
        w.scenes.push_back(std::move(scene));
    }
    w.cameras.push_back(
        {"Hero", technique == 23 ? XMFLOAT3{23, 23, 31} : XMFLOAT3{9, 6, 12}, {0, 1.6f, 0}, 45, .05f, 200});
    w.cameras.push_back({"StressHero",
                         technique == 23 ? XMFLOAT3{42, 40, 58} : XMFLOAT3{30, 22, 38},
                         {0, 1.6f, 0},
                         45,
                         .05f,
                         250});
    w.path.assign(1201, w.cameras.front());
    w.exposure = 1;
    w.environmentIntensity = .6f;
    return w;
}
inline ClusterSceneData MakeClusterSceneData(Workshop &w, int technique, bool fixture = false)
{
    using namespace DirectX;
    if (fixture && technique != 23)
    {
        auto &p = w.primitives[2];
        p.vertices.clear();
        p.indices.clear();
        for (uint32_t y = 0; y <= 8; ++y)
            for (uint32_t x = 0; x <= 8; ++x)
            {
                float u = x / 8.f, v = y / 8.f;
                p.vertices.push_back({technique == 21 ? XMFLOAT3{(u - .5f) * 4, 1 + v * 2, 0}
                                                      : XMFLOAT3{(u - .5f) * 4, 1, (v - .5f) * 4},
                                      {u, v},
                                      technique == 21 ? XMFLOAT3{0, 0, 1} : XMFLOAT3{0, 1, 0},
                                      {1, 0, 0, 1}});
            }
        for (uint32_t y = 0; y < 8; ++y)
            for (uint32_t x = 0; x < 8; ++x)
            {
                uint32_t a = y * 9 + x, b = a + 1, c = a + 9, d = c + 1;
                if (technique == 21)
                    p.indices.insert(p.indices.end(), {a, b, c, b, d, c});
                else
                    p.indices.insert(p.indices.end(), {a, c, b, b, c, d});
            }
    }
    ClusterSceneData result;
    for (uint32_t primitive = 0; primitive < w.primitives.size(); ++primitive)
    {
        auto &p = w.primitives[primitive];
        float minimumY = p.vertices.front().position.y, maximumY = minimumY;
        for (const auto &vertex : p.vertices)
        {
            minimumY = std::min(minimumY, vertex.position.y);
            maximumY = std::max(maximumY, vertex.position.y);
        }
        if (technique == 22 && primitive == 2)
            break;
        const auto first = uint32_t(result.clusters.size());
        const auto beginV = uint32_t(result.rays.vertices.size()),
                   beginI = uint32_t(result.rays.indices.size());
        size_t bound = meshopt_buildMeshletsBound(p.indices.size(), 256, 32);
        std::vector<meshopt_Meshlet> meshlets(bound);
        std::vector<unsigned> vertices(p.indices.size());
        std::vector<unsigned char> triangles(p.indices.size());
        const size_t count = meshopt_buildMeshletsSpatial(
            meshlets.data(), vertices.data(), triangles.data(), p.indices.data(), p.indices.size(),
            &p.vertices[0].position.x, p.vertices.size(), sizeof(PbrVertex), 256, 32, 128, .5f);
        uint64_t total = 0;
        for (size_t m = 0; m < count; ++m)
        {
            auto &c = meshlets[m];
            meshopt_optimizeMeshlet(vertices.data() + c.vertex_offset, triangles.data() + c.triangle_offset,
                                    c.triangle_count, c.vertex_count);
            ClusterGeometry cluster{{uint32_t(result.rays.vertices.size()),
                                     uint32_t(result.rays.indices.size()), c.triangle_count, c.vertex_count},
                                    {primitive, uint32_t(result.clusters.size()),
                                     uint32_t(technique == 21 && primitive == 2), 0}};
            if (cluster.extra.z == 1)
                cluster.extra.w = std::bit_cast<uint32_t>(maximumY - minimumY);
            for (uint32_t i = 0; i < c.vertex_count; ++i)
            {
                const auto &v = p.vertices[vertices[c.vertex_offset + i]];
                result.rays.vertices.push_back({{v.position.x, v.position.y, v.position.z, v.uv.x},
                                                {v.normal.x, v.normal.y, v.normal.z, v.uv.y},
                                                v.tangent});
            }
            for (uint32_t i = 0; i < c.triangle_count * 3; ++i)
                result.rays.indices.push_back(triangles[c.triangle_offset + i]);
            total += c.triangle_count;
            result.clusters.push_back(cluster);
        }
        if (total != p.indices.size() / 3)
            throw std::runtime_error("Cluster partition lost triangles");
        result.ranges.push_back({first, uint32_t(count)});
        result.rays.primitives.push_back({{beginV, beginI, p.material, uint32_t(p.indices.size())}});
    }
    if (technique == 22)
    {
        // The 9x9 source grid is shared by all 2/4/8-rate topologies. Edge bits:
        // south/east/north/west; coarse boundary cells use center fans to retain every midpoint.
        for (uint32_t rate : {2u, 4u, 8u})
            for (uint32_t mask = 0; mask < (rate == 8 ? 1u : 16u); ++mask)
            {
                std::vector<uint32_t> ix;
                const uint32_t step = 8 / rate;
                auto tri = [&](uint32_t a, uint32_t b, uint32_t c) { ix.insert(ix.end(), {a, c, b}); };
                for (uint32_t y = 0; y < 8; y += step)
                    for (uint32_t x = 0; x < 8; x += step)
                    {
                        uint32_t a = y * 9 + x, b = a + step, c = a + step * 9, d = c + step;
                        const bool s = y == 0 && (mask & 1), e = x + step == 8 && (mask & 2),
                                   n = y + step == 8 && (mask & 4), v = x == 0 && (mask & 8);
                        if (!(s || e || n || v))
                        {
                            tri(a, b, c);
                            tri(b, d, c);
                            continue;
                        }
                        std::vector<uint32_t> boundary{a};
                        if (s)
                            boundary.push_back(a + step / 2);
                        boundary.push_back(b);
                        if (e)
                            boundary.push_back(b + step / 2 * 9);
                        boundary.push_back(d);
                        if (n)
                            boundary.push_back(c + step / 2);
                        boundary.push_back(c);
                        if (v)
                            boundary.push_back(a + step / 2 * 9);
                        const uint32_t center = (y + step / 2) * 9 + x + step / 2;
                        for (size_t k = 0; k < boundary.size(); ++k)
                            tri(center, boundary[k], boundary[(k + 1) % boundary.size()]);
                    }
                const std::set<uint32_t> unique(ix.begin(), ix.end());
                result.topologies.push_back({uint32_t(result.rays.indices.size()), uint32_t(ix.size() / 3),
                                             uint32_t(unique.size()), rate, mask});
                result.rays.indices.insert(result.rays.indices.end(), ix.begin(), ix.end());
            }
        result.tileFirst = uint32_t(result.clusters.size());
        result.tileSide = fixture ? 2 : 8;
        result.tileCount = result.tileSide * result.tileSide;
        const auto &topo = result.topologies.back();
        for (uint32_t i = 0; i < result.tileCount; ++i)
        {
            const uint32_t begin = uint32_t(result.rays.vertices.size());
            for (uint32_t y = 0; y < 9; ++y)
                for (uint32_t x = 0; x < 9; ++x)
                {
                    float px = (float(i % result.tileSide) - result.tileSide * .5f) * 2 + x * .25f,
                          pz = (float(i / result.tileSide) - result.tileSide * .5f) * 2 + y * .25f;
                    result.rays.vertices.push_back({{px, 1, pz, x / 8.f}, {0, 1, 0, y / 8.f}, {1, 0, 0, 1}});
                }
            result.clusters.push_back(
                {{begin, topo.indexOffset, topo.triangles, topo.uniqueVertices}, {2, 0, 2, i}});
        }
        result.ranges.push_back({result.tileFirst, result.tileCount});
        result.rays.primitives.push_back({{0, 0, w.primitives[2].material, 0}});
        for (auto &scene : w.scenes)
        {
            const int side = scene.name == "Stress" ? 3 : 1;
            for (int z = 0; z < side; ++z)
                for (int x = 0; x < side; ++x)
                {
                    WorkshopInstance tile;
                    tile.id = "terrain:" + std::to_string(z * side + x);
                    tile.primitive = 2;
                    XMStoreFloat4x4(&tile.world, XMMatrixTranslation((x - (side - 1) * .5f) * 16, 0,
                                                                     (z - (side - 1) * .5f) * 16));
                    scene.instances.push_back(tile);
                }
        }
    }
    auto materials = MakeRaySceneData(w).materials;
    result.rays.materials = std::move(materials);
    return result;
}
inline void SelectClusterLod(ClusterSceneData &data, const DirectX::XMFLOAT3 &eye, uint32_t height, float fov,
                             int fixedRate)
{
    if (!data.tileCount)
        return;
    std::vector<uint32_t> levels(data.tileCount);
    for (uint32_t i = 0; i < data.tileCount; ++i)
    {
        float x = (float(i % data.tileSide) - data.tileSide * .5f) * 2 + 1,
              z = (float(i / data.tileSide) - data.tileSide * .5f) * 2 + 1;
        float d =
            std::sqrt((eye.x - x) * (eye.x - x) + (eye.y - 1) * (eye.y - 1) + (eye.z - z) * (eye.z - z));
        float pixels = float(height) / std::tan(fov * .00872664626f) / std::max(d, 1.f);
        levels[i] = fixedRate ? uint32_t(fixedRate == 2   ? 0
                                         : fixedRate == 4 ? 1
                                                          : 2)
                              : (pixels > 320   ? 2
                                 : pixels > 160 ? 1
                                                : 0);
    }
    for (uint32_t pass = 0; pass < data.tileSide; ++pass)
        for (uint32_t i = 0; i < data.tileCount; ++i)
            for (int d : {-1, 1, -int(data.tileSide), int(data.tileSide)})
            {
                int j = int(i) + d;
                if (j < 0 || j >= int(data.tileCount) ||
                    (std::abs(d) == 1 && i / data.tileSide != uint32_t(j) / data.tileSide))
                    continue;
                if (levels[j] > levels[i] + 1)
                    levels[i] = levels[j] - 1;
            }
    for (uint32_t i = 0; i < data.tileCount; ++i)
    {
        uint32_t mask = 0;
        int x = int(i % data.tileSide), y = int(i / data.tileSide), side = int(data.tileSide);
        if (y > 0 && levels[i - side] > levels[i])
            mask |= 1;
        if (x + 1 < side && levels[i + 1] > levels[i])
            mask |= 2;
        if (y + 1 < side && levels[i + side] > levels[i])
            mask |= 4;
        if (x > 0 && levels[i - 1] > levels[i])
            mask |= 8;
        uint32_t index = levels[i] == 2 ? 32 : levels[i] * 16 + mask;
        const auto &t = data.topologies.at(index);
        auto &c = data.clusters[data.tileFirst + i];
        c.offsets.y = t.indexOffset;
        c.offsets.z = t.triangles;
        c.offsets.w = t.uniqueVertices;
        c.extra.y = data.tileFirst + index;
    }
}
inline std::vector<RayInstance> MakeClusterInstances(const Workshop &w, const ClusterSceneData &data,
                                                     int scene)
{
    auto instances = MakeRayInstances(w, scene, 1, 0);
    for (auto &i : instances)
        i.primitive.y = data.ranges.at(i.primitive.x).x;
    return instances;
}
} // namespace tutorial
