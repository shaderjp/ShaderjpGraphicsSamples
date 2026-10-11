#pragma once
#include "Lighting.h"
#include <cmath>

namespace tutorial
{
// Editable CPU state. No ImGui or graphics API dependency belongs here.
struct LightingControls
{
    float yaw = -42.2737f, elevation = 47.107f;
    float lightColor[3] = {1.0f, 0.95f, 0.9f}; // Linear RGB, multiplied by intensity.
    float intensity = 3.0f, ambient = 0.035f, exposure = 1.0f;
    float metallic = 1.0f, roughness = 1.0f, normal = 1.0f, ao = 1.0f, emissive = 1.0f;
    int mode = 0;
    bool rotate = false;

    void Reset(const GltfModel &model)
    {
        *this = LightingControls{};
        metallic = model.metallicFactor;
        roughness = model.roughnessFactor;
        normal = model.normalScale;
        ao = model.occlusionStrength;
    }

    LightingConstants MakeConstants(uint32_t width, uint32_t height, const GltfModel &model) const
    {
        auto c = MakeLightingConstants(width, height, model, static_cast<uint32_t>(mode), 0);
        const float y = DirectX::XMConvertToRadians(yaw), e = DirectX::XMConvertToRadians(elevation);
        c.lightDirection = {std::sin(y) * std::cos(e), std::sin(e), std::cos(y) * std::cos(e), 0};
        c.lightColor = {lightColor[0] * intensity, lightColor[1] * intensity, lightColor[2] * intensity, 0};
        c.ambientExposure = {ambient, ambient, ambient, exposure};
        c.material = {metallic, roughness, normal, ao};
        c.emissiveFactor = {model.emissiveFactor.x * emissive, model.emissiveFactor.y * emissive,
                            model.emissiveFactor.z * emissive, 0};
        return c;
    }
};
} // namespace tutorial
