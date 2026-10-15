#pragma once
#include "MeshletData.h"
#include "SkinnedGltf.h"
#include "SkinningRootMotion.h"
#include <chrono>
#include <map>
namespace tutorial
{
struct alignas(16) SkinInfluence
{
    DirectX::XMUINT4 joints{};
    DirectX::XMFLOAT4 weights{1, 0, 0, 0};
};
static_assert(sizeof(SkinInfluence) == 32);
struct SkinJointBounds
{
    uint32_t joint = 0;
    DirectX::XMFLOAT3 minimum{FLT_MAX, FLT_MAX, FLT_MAX}, maximum{-FLT_MAX, -FLT_MAX, -FLT_MAX};
};
struct SkinningData
{
    GltfSkinnedModel model;
    SkinningRootMotion rootMotion;
    DirectX::XMFLOAT3 rootOffset{};
    std::array<DirectX::XMFLOAT3, 2> baseMinimum{}, baseMaximum{};
    std::vector<SkinInfluence> influences;
    std::vector<std::vector<SkinJointBounds>> jointBounds;
    std::vector<DirectX::XMFLOAT4X4> palettes;
    std::vector<DirectX::XMFLOAT4> spheres;
    uint32_t vertexCount = 0, jointCount = 0;
    double poseMilliseconds = 0, boundsMilliseconds = 0;
    Workshop Build(const std::filesystem::path &path)
    {
        using namespace DirectX;
        model = LoadGltfSkinned(path);
        rootMotion.Build(model);
        Workshop w;
        for (auto &p : model.primitives)
            if (p.node != model.primitives.front().node || p.skin != model.primitives.front().skin)
                throw std::runtime_error("This viewer requires one shared skinned mesh node");
        jointCount = uint32_t(model.skins[model.primitives.front().skin].joints.size());
        w.images = {{{"$mesh-white"}, ImageSemantic::Color, 0}, {{"$mesh-normal"}, ImageSemantic::Normal, 0}};
        for (auto &m : model.materials)
        {
            WorkshopMaterial o;
            o.id = m.name;
            o.base = m.base;
            o.factors = {m.metallic, m.roughness, 0, 1};
            o.emission = {m.emission.x, m.emission.y, m.emission.z, 0};
            o.images = {0, 1, 0, 0};
            o.alpha = m.mask ? AlphaMode::Mask : AlphaMode::Opaque;
            o.cutoff = m.cutoff;
            o.doubleSided = m.doubleSided;
            w.materials.push_back(o);
        }
        for (auto &p : model.primitives)
        {
            WorkshopPrimitive o;
            o.id = "Robot" + std::to_string(w.primitives.size());
            o.indices = p.indices;
            o.material = p.material;
            o.minimum = {FLT_MAX, FLT_MAX, FLT_MAX};
            o.maximum = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
            for (auto &v : p.vertices)
            {
                o.vertices.push_back({{v.positionU.x, v.positionU.y, v.positionU.z},
                                      {v.positionU.w, v.normalV.w},
                                      {v.normalV.x, v.normalV.y, v.normalV.z},
                                      v.tangent});
                influences.push_back({v.joints, v.weights});
                auto pos = XMVectorSet(v.positionU.x, v.positionU.y, v.positionU.z, 0);
                XMStoreFloat3(&o.minimum, XMVectorMin(XMLoadFloat3(&o.minimum), pos));
                XMStoreFloat3(&o.maximum, XMVectorMax(XMLoadFloat3(&o.maximum), pos));
            }
            w.primitives.push_back(std::move(o));
        }
        vertexCount = uint32_t(influences.size());
        WorkshopMaterial floor;
        floor.id = "Floor";
        floor.images = {0, 1, 0, 0};
        floor.base = {.12f, .15f, .2f, 1};
        floor.factors = {0, .65f, 0, 1};
        floor.doubleSided = true;
        w.materials.push_back(floor);
        WorkshopPrimitive ground;
        ground.id = "Floor";
        ground.material = uint32_t(w.materials.size() - 1);
        ground.minimum = {-15, -.01f, -15};
        ground.maximum = {15, 0, 15};
        ground.vertices = {{{-15, 0, -15}, {0, 0}, {0, 1, 0}, {1, 0, 0, 1}},
                           {{-15, 0, 15}, {0, 1}, {0, 1, 0}, {1, 0, 0, 1}},
                           {{15, 0, 15}, {1, 1}, {0, 1, 0}, {1, 0, 0, 1}},
                           {{15, 0, -15}, {1, 0}, {0, 1, 0}, {1, 0, 0, 1}}};
        ground.indices = {0, 1, 2, 0, 2, 3};
        w.primitives.push_back(ground);
        influences.insert(influences.end(), 4, SkinInfluence{});
        for (int scene = 0; scene < 2; ++scene)
        {
            WorkshopScene s;
            s.name = scene ? "Stress" : "Standard";
            s.minimum = scene ? XMFLOAT3{-12, -.05f, -12} : XMFLOAT3{-3, -.05f, -3};
            s.maximum = scene ? XMFLOAT3{12, 4, 12} : XMFLOAT3{3, 4, 3};
            XMStoreFloat3(&s.direction, XMVector3Normalize(XMVectorSet(-.6f, 1, -.4f, 0)));
            s.sun = {3, 2.85f, 2.7f, 1};
            for (uint32_t actor = 0; actor < (scene ? 64u : 1u); ++actor)
                for (uint32_t primitive = 0; primitive < model.primitives.size(); ++primitive)
                {
                    WorkshopInstance i;
                    i.id = "Actor" + std::to_string(actor) + "." + std::to_string(primitive);
                    i.primitive = primitive;
                    XMStoreFloat4x4(&i.world,
                                    XMMatrixTranslation(scene ? (float(actor % 8) - 3.5f) * 2.6f : 0, 0,
                                                        scene ? (float(actor / 8) - 3.5f) * 2.6f : 0));
                    s.instances.push_back(i);
                }
            WorkshopInstance floorInstance;
            floorInstance.id = "Floor";
            floorInstance.primitive = uint32_t(w.primitives.size() - 1);
            XMStoreFloat4x4(&floorInstance.world, XMMatrixIdentity());
            s.instances.push_back(floorInstance);
            w.scenes.push_back(s);
            baseMinimum[scene] = s.minimum;
            baseMaximum[scene] = s.maximum;
        }
        w.cameras = {{"Hero", {2.2f, 1.8f, -3.1f}, {0, 1, 0}, 40, .05f, 100},
                     {"StressHero", {20, 18, -23}, {0, 1, 0}, 45, .05f, 100},
                     {"OffscreenCaster", {2, 1.2f, -3.5f}, {2, 0, -.3f}, 28, .05f, 100}};
        w.path.assign(1200, w.cameras.front());
        w.environment = AssetPath(L"Assets/Environments/LightingLab/Studio");
        w.environmentIntensity = .8f;
        w.exposure = 1;
        return w;
    }
    void UpdateRootMotion(Workshop &w, size_t clip, double seconds)
    {
        rootOffset = rootMotion.Offset(clip, seconds);
        for (size_t i = 0; i < w.scenes.size(); ++i)
        {
            w.scenes[i].minimum = {baseMinimum[i].x + rootOffset.x, baseMinimum[i].y,
                                   baseMinimum[i].z + rootOffset.z};
            w.scenes[i].maximum = {baseMaximum[i].x + rootOffset.x, baseMaximum[i].y,
                                   baseMaximum[i].z + rootOffset.z};
        }
    }
    void PrepareBounds(const MeshletData &data)
    {
        using namespace DirectX;
        jointBounds.resize(data.meshlets.size());
        for (size_t m = 0; m < data.meshlets.size(); ++m)
        {
            auto &mesh = data.meshlets[m];
            std::map<uint32_t, SkinJointBounds> boxes;
            uint32_t vertexBase = 0;
            for (auto &p : data.primitives)
                if (m >= p.meshlets.x && m < p.meshlets.x + p.meshlets.y)
                {
                    vertexBase = p.offsets.x;
                    break;
                }
            for (uint32_t i = 0; i < mesh.offsets.z; ++i)
            {
                auto vi = vertexBase + data.meshletVertices[mesh.offsets.x + i];
                auto v = data.vertices[vi];
                auto inf = influences[vi];
                for (int k = 0; k < 4; ++k)
                    if ((&inf.weights.x)[k] > 0)
                    {
                        auto ji = (&inf.joints.x)[k];
                        auto &b = boxes[ji];
                        b.joint = ji;
                        auto pos = XMVectorSet(v.positionU.x, v.positionU.y, v.positionU.z, 0);
                        XMStoreFloat3(&b.minimum, XMVectorMin(XMLoadFloat3(&b.minimum), pos));
                        XMStoreFloat3(&b.maximum, XMVectorMax(XMLoadFloat3(&b.maximum), pos));
                    }
            }
            for (auto &[j, b] : boxes)
                jointBounds[m].push_back(b);
        }
        palettes.resize(64 * jointCount + 1);
        XMStoreFloat4x4(&palettes.back(), XMMatrixIdentity());
        spheres.resize(64 * data.meshlets.size());
    }
    void Update(size_t clip, float seconds, bool loop, int scene, const MeshletData &data,
                std::vector<MeshInstance> &instances, bool bounds)
    {
        using namespace DirectX;
        auto start = std::chrono::steady_clock::now();
        uint32_t actors = scene ? 64u : 1u;
        auto duration = model.clips.at(clip).duration;
        std::vector<XMFLOAT4X4> meshWorlds(actors);
        for (uint32_t actor = 0; actor < actors; ++actor)
        {
            float t = seconds + float(actor) * duration / 64;
            t = loop && duration > 0 ? std::fmod(t, duration) : std::min(t, duration);
            auto pose = EvaluateGltfPose(model, clip, t);
            meshWorlds[actor] = pose.globals[model.primitives[0].node];
            auto p = MakeSkinPalette(model, pose, 0);
            std::copy(p.begin(), p.end(), palettes.begin() + actor * jointCount);
        }
        poseMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < instances.size(); ++i)
        {
            auto &instance = instances[i];
            if (instance.primitive.x >= model.primitives.size())
            {
                instance.primitive.y = instance.primitive.w = UINT32_MAX;
                instance.primitive.z = 64 * jointCount;
                continue;
            }
            auto actor = uint32_t(i / model.primitives.size());
            instance.primitive.y = actor * vertexCount;
            instance.primitive.z = actor * jointCount;
            instance.primitive.w = actor * uint32_t(data.meshlets.size());
            auto world = XMLoadFloat4x4(&meshWorlds[actor]) *
                         XMMatrixTranslation(rootOffset.x, rootOffset.y, rootOffset.z) *
                         XMLoadFloat4x4(&instance.world);
            XMStoreFloat4x4(&instance.world, world);
            XMStoreFloat4x4(&instance.normal, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
            if (!bounds)
                continue;
            auto &primitive = data.primitives[instance.primitive.x];
            for (uint32_t m = primitive.meshlets.x; m < primitive.meshlets.x + primitive.meshlets.y; ++m)
            {
                auto lo = XMVectorReplicate(FLT_MAX), hi = XMVectorReplicate(-FLT_MAX);
                for (auto &b : jointBounds[m])
                {
                    auto matrix = XMLoadFloat4x4(&palettes[actor * jointCount + b.joint]);
                    for (int corner = 0; corner < 8; ++corner)
                    {
                        auto v =
                            XMVector3TransformCoord(XMVectorSet(corner & 1 ? b.maximum.x : b.minimum.x,
                                                                corner & 2 ? b.maximum.y : b.minimum.y,
                                                                corner & 4 ? b.maximum.z : b.minimum.z, 1),
                                                    matrix);
                        lo = XMVectorMin(lo, v);
                        hi = XMVectorMax(hi, v);
                    }
                }
                auto center = (lo + hi) * .5f;
                XMFLOAT4 sphere;
                XMStoreFloat4(&sphere, center);
                sphere.w = XMVectorGetX(XMVector3Length(hi - lo)) * .5f + .001f;
                spheres[actor * data.meshlets.size() + m] = sphere;
            }
        }
        boundsMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
};
} // namespace tutorial
