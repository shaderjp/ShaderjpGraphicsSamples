#pragma once
#include "DeferredData.h"
#include <cmath>
namespace tutorial
{
inline constexpr uint32_t LightGridStride = 256, LightGridSlices = 24, MaximumPointLights = 4680;
struct LightGridConstants
{
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4 projection, depth;
    std::array<uint32_t, 4> size{}, controls{};
};
static_assert(sizeof(LightGridConstants) == 128);
struct LightGridSettings
{
    int mode = LIGHT_CULLING, view = 0, perRoom = 256, capacity = 256;
    float radius = 1.6f, intensity = .4f, phase = 0;
    bool animate = false;
};
inline LightGridConstants MakeLightGridConstants(const Workshop &w, int camera, int pathFrame, uint32_t width,
                                                 uint32_t height, const LightGridSettings &g,
                                                 const DirectX::XMFLOAT3 *eyeOverride = nullptr,
                                                 const DirectX::XMFLOAT3 *targetOverride = nullptr,
                                                 const DirectX::XMFLOAT3 *lensOverride = nullptr)
{
    using namespace DirectX;
    const auto &c =
        pathFrame >= 0 ? w.path.at(std::min(size_t(pathFrame), w.path.size() - 1)) : w.cameras.at(camera);
    LightGridConstants result{};
    XMStoreFloat4x4(&result.view, XMMatrixLookAtLH(XMLoadFloat3(eyeOverride ? eyeOverride : &c.position),
                                                   XMLoadFloat3(targetOverride ? targetOverride : &c.target),
                                                   XMVectorSet(0, 1, 0, 0)));
    float y = std::tan(XMConvertToRadians((lensOverride ? lensOverride->x : c.fov)) * .5f);
    result.projection = {
        y * width / height, y,
        (lensOverride ? lensOverride->z : c.farPlane) /
            ((lensOverride ? lensOverride->z : c.farPlane) - (lensOverride ? lensOverride->y : c.nearPlane)),
        -(lensOverride ? lensOverride->y : c.nearPlane) * (lensOverride ? lensOverride->z : c.farPlane) /
            ((lensOverride ? lensOverride->z : c.farPlane) - (lensOverride ? lensOverride->y : c.nearPlane))};
    result.depth = {(lensOverride ? lensOverride->y : c.nearPlane),
                    (lensOverride ? lensOverride->z : c.farPlane), .001f, 0};
    result.size = {(width + 15) / 16, (height + 15) / 16, LightGridSlices, LightGridStride};
    result.controls = {uint32_t(g.mode), uint32_t(g.view), uint32_t(g.capacity), 0};
    return result;
}
inline uint32_t LightGridCellCount(uint32_t width, uint32_t height, int mode)
{
    return ((width + 15) / 16) * ((height + 15) / 16) * (mode == 2 ? LightGridSlices : 1);
}
inline std::vector<WorkshopLight> MakeManyLights(const WorkshopScene &s, const LightGridSettings &g)
{
    auto lights = s.lights;
    const uint32_t rooms = s.name == "Stress" ? 9u : 1u;
    auto unit = [](uint32_t v)
    {
        v ^= v >> 16;
        v *= 0x7feb352d;
        v ^= v >> 15;
        v *= 0x846ca68b;
        v ^= v >> 16;
        return float(v & 0xffffff) / float(0x1000000);
    };
    for (uint32_t room = 0; room < rooms; ++room)
    {
        // Match the authored 9 m x 7 m repeated bays, in the runtime left-handed coordinates.
        float ox = rooms == 1 ? 0 : (int(room % 3) - 1) * 9.f;
        float oz = rooms == 1 ? 0 : (int(room / 3) - 1) * 7.f;
        for (uint32_t i = 0; i < uint32_t(g.perRoom); ++i)
        {
            float x = unit(i * 3 + 17) * 7.2f - 3.6f, z = unit(i * 3 + 18) * 5.2f - 2.6f,
                  y = .35f + unit(i * 3 + 19) * 2.9f;
            if (g.animate)
            {
                x += .25f * std::sin(g.phase + i * .37f);
                z += .25f * std::cos(g.phase + i * .37f);
            }
            const DirectX::XMFLOAT3 colors[] = {
                {1, .36f, .12f}, {.16f, .65f, 1}, {.45f, 1, .5f}, {1, .7f, .25f}};
            const auto c = colors[i % 4];
            lights.push_back({{x + ox, y, z + oz, g.radius}, {c.x, c.y, c.z, g.intensity}});
        }
    }
    if (lights.size() > MaximumPointLights)
        throw std::runtime_error("Point-light capacity exceeded");
    return lights;
}
struct LightGridStatistics
{
    uint32_t active = 0, maximum = 0, overflow = 0;
    double average = 0;
    void Read(const uint32_t *headers, uint32_t cells)
    {
        active = maximum = overflow = 0;
        uint64_t total = 0;
        for (uint32_t i = 0; i < cells; ++i)
        {
            uint32_t n = headers[i * 2];
            if (n)
            {
                ++active;
                total += n;
                maximum = std::max(maximum, n);
            }
            overflow += headers[i * 2 + 1] != 0;
        }
        average = active ? double(total) / active : 0;
    }
};
} // namespace tutorial
