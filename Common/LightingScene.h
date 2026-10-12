#pragma once
#include "Lighting.h"
namespace tutorial
{
struct LightingObject
{
    std::string name;
    GltfModel model;
    DirectX::XMFLOAT3 position{}, rotation{}, scale{1, 1, 1};
    bool castsShadow = true, movable = false;
};
struct LightingScene
{
    std::vector<LightingObject> objects;
    DirectX::XMFLOAT3 camera{}, target{}, direction{}, lightColor{1, 1, 1};
    float fov = 45, intensity = 3, exposure = 1;
    std::array<std::filesystem::path, 2> environments;
};
struct alignas(16) LabConstants
{
    LightingConstants lighting;
    DirectX::XMFLOAT4X4 inverseViewProjection, lightViewProjection;
    DirectX::XMFLOAT4 environment; // intensity, yaw radians, stage, display mode
    DirectX::XMFLOAT4 shadow;      // enabled, PCF, receiver bias, resolution
};
static_assert(sizeof(LabConstants) == 480);
LightingScene LoadLightingScene(const std::filesystem::path &path);
LabConstants MakeLabConstants(uint32_t width, uint32_t height, const LightingScene &scene, size_t object,
                              int technique, int view, float environmentIntensity, float environmentYaw,
                              float directIntensity, float exposure, bool shadows, bool pcf, float bias,
                              float occluderOffset);
} // namespace tutorial
