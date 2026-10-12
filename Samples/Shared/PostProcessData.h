#pragma once
#include "LightingScene.h"
namespace tutorial
{
struct alignas(16) PostConstants
{
    DirectX::XMFLOAT4 settings;   // exposure, threshold, strength, display mode
    DirectX::XMUINT4 dimensions;  // full and ceil-half extent
    DirectX::XMFLOAT4 simulation; // dt, gravity, count, reset
    DirectX::XMFLOAT4 emitter;    // xyz, billboard radius
    DirectX::XMUINT4 flags;       // bloom enabled, blur direction, test image, reserved
};
struct Particle
{
    DirectX::XMFLOAT4 positionSize, velocityAge, color;
};
static_assert(sizeof(PostConstants) == 80 && sizeof(Particle) == 48);
inline LightingScene SecondaryScene(const LightingScene &s)
{
    auto copy = s;
    copy.camera = s.secondaryCamera;
    copy.target = s.secondaryTarget;
    copy.fov = s.secondaryFov;
    return copy;
}
} // namespace tutorial
