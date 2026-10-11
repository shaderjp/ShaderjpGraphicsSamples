#pragma once
#include "Gltf.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>

namespace tutorial
{
struct PbrVertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT2 uv;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT4 tangent;
};
static_assert(sizeof(PbrVertex) == 48 && offsetof(PbrVertex, tangent) == 32);

// CPU data and matrix calculation only. API resources stay in each Sample.cpp.
struct alignas(16) LightingConstants
{
    DirectX::XMFLOAT4X4 world, viewProjection, normalMatrix;
    DirectX::XMFLOAT4 baseColorFactor, cameraPosition, lightDirection, lightColor;
    DirectX::XMFLOAT4 ambientExposure, material, emissiveFactor, shading;
};
static_assert(sizeof(LightingConstants) == 320);
static_assert(offsetof(LightingConstants, baseColorFactor) == 192);
static_assert(offsetof(LightingConstants, shading) == 304);

inline std::vector<PbrVertex> MakePbrVertices(const GltfModel &model)
{
    if (model.tangents.size() != model.mesh.vertices.size())
        throw std::runtime_error("PBR vertices require a tangent for every vertex");
    std::vector<PbrVertex> result;
    result.reserve(model.mesh.vertices.size());
    for (size_t i = 0; i < model.mesh.vertices.size(); ++i)
    {
        const auto &v = model.mesh.vertices[i];
        result.push_back({v.position, v.uv, v.normal, model.tangents[i]});
    }
    return result;
}

inline LightingConstants MakeLightingConstants(uint32_t width, uint32_t height, const GltfModel &model,
                                               uint32_t mode, float lightAngle)
{
    using namespace DirectX;
    const auto minimum = XMLoadFloat3(&model.minimum), maximum = XMLoadFloat3(&model.maximum);
    const auto center = (minimum + maximum) * 0.5f;
    const auto extent = maximum - minimum;
    const float radius =
        std::max({XMVectorGetX(extent), XMVectorGetY(extent), XMVectorGetZ(extent), 0.002f}) * 0.5f;
    const float aspect = float(width) / float(height);
    const float distance = radius * 3.8f / std::min(1.0f, aspect);
    const auto direction = XMVector3Normalize(XMVectorSet(-0.82f, 0.32f, 0.56f, 0));
    const auto camera = center + direction * distance;
    const auto world = XMMatrixIdentity();
    const auto view = XMMatrixLookAtLH(camera, center, XMVectorSet(0, 1, 0, 0));
    const auto projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, std::max(0.001f, distance * 0.01f),
                                                     distance + radius * 10);
    const auto light = XMVector3Normalize(
        XMVector3TransformNormal(XMVectorSet(-0.5f, 0.8f, 0.55f, 0), XMMatrixRotationY(lightAngle)));
    LightingConstants c{};
    XMStoreFloat4x4(&c.world, world);
    XMStoreFloat4x4(&c.viewProjection, view * projection);
    XMStoreFloat4x4(&c.normalMatrix, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
    c.baseColorFactor = model.baseColorFactor;
    XMStoreFloat4(&c.cameraPosition, camera);
    XMStoreFloat4(&c.lightDirection,
                  light); // Surface toward directional light, not the ray travel direction.
    c.lightColor = {3.0f, 2.85f, 2.7f, 0};
    c.ambientExposure = {0.035f, 0.035f, 0.035f, 1.0f};
    c.material = {model.metallicFactor, model.roughnessFactor, model.normalScale, model.occlusionStrength};
    c.emissiveFactor = {model.emissiveFactor.x, model.emissiveFactor.y, model.emissiveFactor.z, 0};
    c.shading = {float(mode), 32.0f, 0.3f, 0}; // Mode, Blinn exponent, Blinn specular strength.
    return c;
}
} // namespace tutorial
