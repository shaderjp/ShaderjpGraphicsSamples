#pragma once
#include "Workshop.h"
#include <algorithm>
#include <cstddef>
namespace tutorial
{
inline constexpr uint32_t RayTextureCount = 64;
struct alignas(16) RayVertex
{
    DirectX::XMFLOAT4 positionU, normalV, tangent;
};
struct alignas(16) RayPrimitive
{
    DirectX::XMUINT4 offsets;
}; // vertex, index, material, index count
struct alignas(16) RayMaterial
{
    DirectX::XMFLOAT4 base, factors, emission;
    DirectX::XMUINT4 images;
    DirectX::XMFLOAT4 alpha; // cutoff, MASK, double-sided, reserved
};
struct alignas(16) RayInstance
{
    DirectX::XMFLOAT4X4 world, normalMatrix;
    DirectX::XMUINT4 primitive;
};
struct alignas(16) RayConstants
{
    DirectX::XMFLOAT4X4 inverseViewProjection, viewProjection;
    DirectX::XMFLOAT4 camera, sunDirection, sunColor, environment, screen;
    DirectX::XMUINT4 controls, lighting;
};
static_assert(sizeof(RayVertex) == 48 && sizeof(RayPrimitive) == 16 && sizeof(RayMaterial) == 80);
static_assert(sizeof(RayInstance) == 144 && sizeof(RayConstants) == 240);
static_assert(offsetof(RayConstants, controls) == 208 && offsetof(RayMaterial, images) == 48);
struct RaySceneData
{
    std::vector<RayVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<RayPrimitive> primitives;
    std::vector<RayMaterial> materials;
};
inline RaySceneData MakeRaySceneData(const Workshop &w)
{
    if (w.images.empty() || w.images.size() > RayTextureCount)
        throw std::runtime_error("Raytracing requires 1..64 material textures");
    for (const auto &scene : w.scenes)
        if (scene.lights.size() > 72)
            throw std::runtime_error("Raytracing supports at most 72 point lights");
    RaySceneData result;
    for (const auto &p : w.primitives)
    {
        result.primitives.push_back({{uint32_t(result.vertices.size()), uint32_t(result.indices.size()),
                                      p.material, uint32_t(p.indices.size())}});
        for (const auto &v : p.vertices)
            result.vertices.push_back({{v.position.x, v.position.y, v.position.z, v.uv.x},
                                       {v.normal.x, v.normal.y, v.normal.z, v.uv.y},
                                       v.tangent});
        result.indices.insert(result.indices.end(), p.indices.begin(), p.indices.end());
    }
    for (const auto &m : w.materials)
        result.materials.push_back({m.base,
                                    m.factors,
                                    m.emission,
                                    {m.images[0], m.images[1], m.images[2], m.images[3]},
                                    {m.cutoff, float(m.alpha == AlphaMode::Mask), float(m.doubleSided), 0}});
    return result;
}
inline std::vector<RayInstance> MakeRayInstances(const Workshop &w, int scene, int preset, float occluder)
{
    using namespace DirectX;
    std::vector<RayInstance> result;
    for (const auto &i : w.scenes.at(scene).instances)
    {
        const auto &m = w.materials.at(w.primitives.at(i.primitive).material);
        if (m.alpha == AlphaMode::Blend || (m.alpha == AlphaMode::Mask && preset == 0))
            continue;
        auto world = XMLoadFloat4x4(&i.world);
        if (i.movable)
            world *= XMMatrixTranslation(occluder, 0, 0);
        RayInstance r{};
        XMStoreFloat4x4(&r.world, world);
        XMStoreFloat4x4(&r.normalMatrix, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
        r.primitive = {i.primitive, 0, 0, 0};
        result.push_back(r);
    }
    if (result.empty() || result.size() > 0xffffff)
        throw std::runtime_error("Invalid raytracing instance count");
    return result;
}
inline RayConstants MakeRayConstants(const Workshop &w, int scene, int camera, int pathFrame, uint32_t width,
                                     uint32_t height, uint32_t view, bool shadows, bool reflections,
                                     bool direct, bool ibl, float intensity, float yaw, float bias,
                                     const DirectX::XMFLOAT3 *eyeOverride = nullptr,
                                     const DirectX::XMFLOAT3 *targetOverride = nullptr,
                                     const DirectX::XMFLOAT3 *lensOverride = nullptr)
{
    using namespace DirectX;
    const auto &s = w.scenes.at(scene);
    const auto &cam =
        pathFrame >= 0 ? w.path.at(std::min(size_t(pathFrame), w.path.size() - 1)) : w.cameras.at(camera);
    auto vp =
        XMMatrixLookAtLH(XMLoadFloat3(eyeOverride ? eyeOverride : &cam.position),
                         XMLoadFloat3(targetOverride ? targetOverride : &cam.target),
                         XMVectorSet(0, 1, 0, 0)) *
        XMMatrixPerspectiveFovLH(XMConvertToRadians((lensOverride ? lensOverride->x : cam.fov)),
                                 float(width) / height, (lensOverride ? lensOverride->y : cam.nearPlane),
                                 (lensOverride ? lensOverride->z : cam.farPlane));
    RayConstants c{};
    XMStoreFloat4x4(&c.viewProjection, vp);
    XMStoreFloat4x4(&c.inverseViewProjection, XMMatrixInverse(nullptr, vp));
    const auto &eye = eyeOverride ? *eyeOverride : cam.position;
    c.camera = {eye.x, eye.y, eye.z, 0};
    c.sunDirection = {s.direction.x, s.direction.y, s.direction.z, 0};
    c.sunColor = s.sun;
    c.environment = {intensity, XMConvertToRadians(yaw), 0, 0};
    c.screen = {float(width), float(height),
                XMVectorGetX(XMVector3Length(XMLoadFloat3(&s.maximum) - XMLoadFloat3(&s.minimum))) + 4, bias};
    c.controls = {uint32_t(s.lights.size()), view, uint32_t(shadows), uint32_t(reflections)};
    c.lighting = {uint32_t(direct), uint32_t(ibl), 0, 0};
    return c;
}
} // namespace tutorial
