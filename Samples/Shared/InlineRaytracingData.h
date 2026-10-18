#pragma once
#include <chrono>
#include "RaytracingFixtures.h"
namespace tutorial
{
// CPU data only. Each API sample owns its acceleration structures and synchronization.
struct alignas(16) InlineConstants
{
    DirectX::XMUINT4 controls; // sun RayQuery, shadowed point-light count, reflections, diagnostic
    DirectX::XMFLOAT4 ray;     // origin bias, maximum distance, reserved
};
static_assert(sizeof(InlineConstants) == 32);
inline Workshop MakeInlineWorkshop(Workshop w, const Options &options)
{
    const bool reflectedSun = options.rayTestScene == L"reflection-sun-shadow";
    const bool back = options.rayTestScene == L"reflection-mask-back" ||
                      options.rayTestScene == L"reflection-single-back";
    const bool self = options.rayTestScene == L"sun-unoccluded";
    w = RayFixture(std::move(w), reflectedSun ? L"reflection-shadow" :
                                back ? L"reflection-mask-solid" :
                                self ? L"sun-shadow" : options.rayTestScene);
    if (back)
    {
        using namespace DirectX;
        for (auto &scene : w.scenes)
            XMStoreFloat4x4(&scene.instances[1].world,
                           XMMatrixRotationY(-XM_PIDIV2) * XMMatrixTranslation(2, 0, 0));
        w.materials[1].doubleSided = options.rayTestScene == L"reflection-mask-back";
    }
    if (self)
        for (auto &scene : w.scenes)
            scene.instances.resize(1);
    if (reflectedSun)
    {
        for (auto &scene : w.scenes)
        {
            scene.lights.clear();
            scene.sun = {1, 1, 1, 1};
            DirectX::XMStoreFloat3(&scene.direction,
                                   DirectX::XMVector3Normalize(DirectX::XMVectorSet(-1, 1, -1, 0)));
        }
    }
    return w;
}
} // namespace tutorial
