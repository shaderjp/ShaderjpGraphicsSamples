#pragma once
#include "Workshop.h"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
namespace tutorial
{
struct alignas(16) DeferredConstants
{
    DirectX::XMFLOAT4X4 world, viewProjection, normalMatrix, inverseViewProjection, lightViewProjection;
    DirectX::XMFLOAT4 base, material, emission, camera, sunDirection, sunColor, environment, shadow, screen;
    std::array<uint32_t, 4> controls{};
};
static_assert(sizeof(DeferredConstants) == 480);
inline DirectX::XMMATRIX ShadowProjection(const WorkshopScene &s)
{
    using namespace DirectX;
    auto lo = XMLoadFloat3(&s.minimum), hi = XMLoadFloat3(&s.maximum), center = (lo + hi) * .5f;
    float radius = XMVectorGetX(XMVector3Length(hi - lo)) * .5f + 2;
    auto dir = XMLoadFloat3(&s.direction);
    auto up = std::abs(s.direction.y) > .99f ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);
    auto view = XMMatrixLookAtLH(center + dir * (radius + 2), center, up);
    XMFLOAT3 minimum{FLT_MAX, FLT_MAX, FLT_MAX}, maximum{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (int c = 0; c < 8; ++c)
    {
        auto p = XMVector3TransformCoord(XMVectorSet(c & 1 ? s.maximum.x : s.minimum.x,
                                                     c & 2 ? s.maximum.y : s.minimum.y,
                                                     c & 4 ? s.maximum.z : s.minimum.z, 1),
                                         view);
        XMStoreFloat3(&minimum, XMVectorMin(XMLoadFloat3(&minimum), p));
        XMStoreFloat3(&maximum, XMVectorMax(XMLoadFloat3(&maximum), p));
    }
    return view * XMMatrixOrthographicOffCenterLH(minimum.x - 2, maximum.x + 2, minimum.y - 2, maximum.y + 2,
                                                  std::max(.01f, minimum.z - 2), maximum.z + 2);
}
inline DeferredConstants MakeDeferredConstants(const Workshop &w, int scene, int camera, int pathFrame,
                                               uint32_t width, uint32_t height,
                                               const WorkshopInstance *instance, uint32_t view, bool direct,
                                               bool ibl, bool shadows, bool pcf, float bias, float exposure,
                                               float environmentIntensity, float yaw, float occluder,
                                               const DirectX::XMFLOAT3 *eyeOverride = nullptr,
                                               const DirectX::XMFLOAT3 *targetOverride = nullptr,
                                               const DirectX::XMFLOAT3 *lensOverride = nullptr)
{
    using namespace DirectX;
    const auto &s = w.scenes.at(scene);
    const auto &cam =
        pathFrame >= 0 ? w.path.at(std::min(size_t(pathFrame), w.path.size() - 1)) : w.cameras.at(camera);
    auto world = instance ? XMLoadFloat4x4(&instance->world) : XMMatrixIdentity();
    if (instance && instance->movable)
        world *= XMMatrixTranslation(occluder, 0, 0);
    auto vp =
        XMMatrixLookAtLH(XMLoadFloat3(eyeOverride ? eyeOverride : &cam.position),
                         XMLoadFloat3(targetOverride ? targetOverride : &cam.target),
                         XMVectorSet(0, 1, 0, 0)) *
        XMMatrixPerspectiveFovLH(XMConvertToRadians((lensOverride ? lensOverride->x : cam.fov)),
                                 float(width) / height, (lensOverride ? lensOverride->y : cam.nearPlane),
                                 (lensOverride ? lensOverride->z : cam.farPlane));
    DeferredConstants c{};
    XMStoreFloat4x4(&c.world, world);
    XMStoreFloat4x4(&c.normalMatrix, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
    XMStoreFloat4x4(&c.viewProjection, vp);
    XMStoreFloat4x4(&c.inverseViewProjection, XMMatrixInverse(nullptr, vp));
    XMStoreFloat4x4(&c.lightViewProjection, ShadowProjection(s));
    if (instance)
    {
        const auto &m = w.materials.at(w.primitives.at(instance->primitive).material);
        c.base = m.base;
        c.material = m.factors;
        c.emission = m.emission;
        c.emission.w = float(m.alpha == AlphaMode::Mask);
        c.screen.w = m.cutoff;
    }
    const auto &eye = eyeOverride ? *eyeOverride : cam.position;
    c.camera = {eye.x, eye.y, eye.z, 1};
    c.sunDirection = {s.direction.x, s.direction.y, s.direction.z, 0};
    c.sunColor = s.sun;
    c.environment = {environmentIntensity, XMConvertToRadians(yaw), exposure, 0};
    c.shadow = {float(shadows), float(pcf), bias, 2048};
    c.screen.x = float(width);
    c.screen.y = float(height);
    c.controls = {uint32_t(s.lights.size()), view, uint32_t(direct), uint32_t(ibl)};
    return c;
}
inline constexpr const char *GpuPassNames[] = {"shadow",
                                               "geometry",
                                               "lighting",
                                               "sky",
                                               "transparent",
                                               "extract",
                                               "horizontal",
                                               "vertical",
                                               "display",
                                               "total"
#ifdef LIGHT_CULLING
                                               ,
                                               "tile-depth",
                                               "cull"
#endif
};
inline constexpr size_t GpuPassCount = std::size(GpuPassNames);
struct DeferredTiming
{
    std::array<double, GpuPassCount> milliseconds{};
    std::vector<std::array<double, GpuPassCount>> samples;
    std::filesystem::path output;
    std::string api, gpu, mode, scene, configuration;
    uint32_t frame = 0;
    bool benchmark = false;
#ifdef LIGHT_CULLING
    uint32_t pointLights = 0, cells = 0, listCapacity = 256, activeCells = 0, maxCandidates = 0,
             overflowCells = 0;
    double averageCandidates = 0;
    int lightsPerRoom = 256;
    float lightRadius = 1.6f, lightIntensity = .4f;
#endif
    void Record(const std::array<double, GpuPassCount> &values)
    {
        milliseconds = values;
        if (benchmark && frame >= 60 && frame < 1260)
            samples.push_back(values);
        ++frame;
    }
    void Save() const
    {
        if (output.empty())
            return;
        nlohmann::json report = {{"api", api},
                                 {"gpu", gpu},
                                 {"mode", mode},
                                 {"scene", scene},
                                 {"configuration", configuration},
                                 {"msvcFullVersion", _MSC_FULL_VER},
                                 {"windowsSdk", "10.0.26100.0"},
                                 {"pointLights", scene == "Stress" ? 72 : 8},
                                 {"width", 1280},
                                 {"height", 720},
                                 {"preset", "OpaqueMask"},
                                 {"warmupFrames", 60},
                                 {"pathFrames", 1200},
                                 {"fixedDelta", 1.0 / 60},
                                 {"gBufferBytesPerPixel", 18}};
#ifdef LIGHT_CULLING
        report["pointLights"] = pointLights;
        report["tileSize"] = 16;
        report["depthSlices"] = 24;
        report["cells"] = cells;
        report["listStride"] = 256;
        report["listCapacity"] = listCapacity;
        report["lightsPerRoom"] = lightsPerRoom;
        report["lightRadius"] = lightRadius;
        report["lightIntensity"] = lightIntensity;
        report["activeCells"] = activeCells;
        report["averageCandidates"] = averageCandidates;
        report["maxCandidates"] = maxCandidates;
        report["overflowCells"] = overflowCells;
        report["listBytes"] = uint64_t(cells) * (256 * 4 + 8);
#endif
#ifdef VISIBILITY_BUFFER
        report["gBufferBytesPerPixel"] = 0;
        report["visibilityBytesPerPixel"] = 12;
        report["representation"] = "VisibilityBuffer";
#else
        report["representation"] = "GBuffer";
#endif
        for (size_t p = 0; p < GpuPassCount; ++p)
        {
            std::vector<double> v;
            double sum = 0;
            for (auto &s : samples)
            {
                v.push_back(s[p]);
                sum += s[p];
            }
            std::sort(v.begin(), v.end());
            if (!v.empty())
                report["passes"][GpuPassNames[p]] = {
                    {"averageMs", sum / v.size()},
                    {"medianMs", v[v.size() / 2]},
                    {"p95Ms", v[std::min(v.size() - 1, size_t(v.size() * .95))]}};
        }
        report["measuredFrames"] = samples.size();
        std::filesystem::create_directories(output.parent_path());
        std::ofstream(output) << report.dump(2);
        std::ofstream csv(output.parent_path() / (output.stem().string() + ".csv"));
        csv << "frame";
        for (auto name : GpuPassNames)
            csv << ',' << name;
        csv << '\n';
        for (size_t f = 0; f < samples.size(); ++f)
        {
            csv << f;
            for (auto v : samples[f])
                csv << ',' << v;
            csv << '\n';
        }
    }
};
} // namespace tutorial
