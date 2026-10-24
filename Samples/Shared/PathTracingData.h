#pragma once
#include "RaytracingFixtures.h"
#include <cmath>
#include <map>
namespace tutorial
{
inline Workshop PathFixture(Workshop w, const std::wstring &name)
{
    if (!name.starts_with(L"pt-")) return RayFixture(std::move(w), name);
    if (name != L"pt-emission" && name != L"pt-dark" && name != L"pt-area" &&
        name != L"pt-transformed" && name != L"pt-occluded" && name != L"pt-mask")
        throw std::runtime_error("Unknown --rt-test-scene");
    w = RayFixture(std::move(w), L"albedo");
    w.materials[0].images[0] = 0;
    w.materials[0].base = {.8f, .4f, .2f, 1};
    if (name == L"pt-emission") { w.materials[0].base = {0, 0, 0, 1}; w.materials[0].emission = {.25f, .5f, 1, 0}; }
    if (name != L"pt-dark" && name != L"pt-emission") {
        auto emitter = w.primitives[0]; emitter.material = 1;
        for (auto &v : emitter.vertices) { v.position.x *= .375f; v.position.y *= .375f; }
        w.primitives.push_back(emitter);
        w.materials[1].base = {0, 0, 0, 1}; w.materials[1].emission = {3, 3, 3, 0};
        auto light = w.scenes[0].instances[0]; light.primitive = 1;
        auto world = DirectX::XMMatrixRotationY(DirectX::XM_PI) * DirectX::XMMatrixTranslation(2, 0, -2);
        if (name == L"pt-transformed") world = DirectX::XMMatrixScaling(1.4f, 1.4f, 1.4f) *
            DirectX::XMMatrixRotationY(DirectX::XM_PI) * DirectX::XMMatrixRotationZ(.4f) * DirectX::XMMatrixTranslation(2.2f, .4f, -2.5f);
        DirectX::XMStoreFloat4x4(&light.world, world); w.scenes[0].instances.push_back(light);
        if (name == L"pt-occluded" || name == L"pt-mask") {
            auto blocker = emitter; blocker.material = 2; w.primitives.push_back(blocker);
            w.materials[2].base = {0, 0, 0, 1};
            if (name == L"pt-mask") { w.materials[2].alpha = AlphaMode::Mask; w.materials[2].images[0] = 4; }
            auto instance = light; instance.primitive = 2;
            DirectX::XMStoreFloat4x4(&instance.world, DirectX::XMMatrixTranslation(1, 0, -1));
            w.scenes[0].instances.push_back(instance);
        }
        w.scenes[1] = w.scenes[0];
    }
    return w;
}
// CPU scene conversion only. GPU resources and synchronization stay in each executable.
inline Workshop MergePathScenes(Workshop room, const Workshop &workshop)
{
    const uint32_t imageBase = uint32_t(room.images.size()), materialBase = uint32_t(room.materials.size()),
                   primitiveBase = uint32_t(room.primitives.size());
    room.images.insert(room.images.end(), workshop.images.begin(), workshop.images.end());
    for (auto m : workshop.materials) { for (auto &i : m.images) i += imageBase; room.materials.push_back(m); }
    for (auto p : workshop.primitives) { p.material += materialBase; room.primitives.push_back(std::move(p)); }
    for (auto s : workshop.scenes) {
        for (auto &i : s.instances) i.primitive += primitiveBase;
        room.scenes.push_back(std::move(s));
    }
    room.cameras.insert(room.cameras.end(), workshop.cameras.begin(), workshop.cameras.end());
    room.path = workshop.path;
    // The room has a black world; the Workshop raw environment is shared and scaled to zero there.
    room.environment = workshop.environment;
    return room;
}
struct alignas(16) PathLight
{
    DirectX::XMFLOAT4 originArea, edge1Cdf, edge2Probability;
    DirectX::XMUINT4 ids; // instance, triangle, material, double-sided
};
static_assert(sizeof(PathLight) == 64 && offsetof(PathLight, ids) == 48);
struct PathLightData { std::vector<PathLight> lights; uint32_t count = 0; };
inline PathLightData MakePathLights(const Workshop &w, std::vector<RayInstance> &instances)
{
    using namespace DirectX;
    std::vector<double> power(w.materials.size());
    std::map<uint32_t, XMFLOAT3> means;
    auto linear = [](uint8_t v) { double x = v / 255.0; return x <= .04045 ? x / 12.92 : std::pow((x + .055) / 1.055, 2.4); };
    for (size_t i = 0; i < w.materials.size(); ++i) {
        const auto &m = w.materials[i];
        if (m.emission.x + m.emission.y + m.emission.z <= 0) continue;
        auto image = m.images[3];
        if (!means.contains(image)) {
            auto pixels = RayTextureImage(w.images.at(image));
            double r = 0, g = 0, b = 0;
            for (size_t p = 0; p < pixels.rgba.size(); p += 4) {
                r += linear(pixels.rgba[p]); g += linear(pixels.rgba[p + 1]); b += linear(pixels.rgba[p + 2]);
            }
            double n = double(pixels.width) * pixels.height;
            means[image] = {float(r / n), float(g / n), float(b / n)};
        }
        auto a = means.at(image);
        power[i] = .2126 * m.emission.x * a.x + .7152 * m.emission.y * a.y + .0722 * m.emission.z * a.z;
    }
    PathLightData result;
    double total = 0;
    std::vector<double> weights;
    for (uint32_t i = 0; i < instances.size(); ++i) {
        auto &inst = instances[i]; inst.primitive.y = UINT32_MAX;
        const auto &p = w.primitives.at(inst.primitive.x);
        if (power[p.material] <= 0) continue;
        inst.primitive.y = uint32_t(result.lights.size());
        auto world = XMLoadFloat4x4(&inst.world);
        for (uint32_t t = 0; t < p.indices.size() / 3; ++t) {
            auto vertex = [&](uint32_t corner) { return XMVector3TransformCoord(XMLoadFloat3(&p.vertices[p.indices[t * 3 + corner]].position), world); };
            auto a = vertex(0), e1 = vertex(1) - a, e2 = vertex(2) - a;
            float area = .5f * XMVectorGetX(XMVector3Length(XMVector3Cross(e1, e2)));
            PathLight light{};
            XMStoreFloat4(&light.originArea, a); light.originArea.w = area;
            XMStoreFloat4(&light.edge1Cdf, e1); XMStoreFloat4(&light.edge2Probability, e2);
            light.ids = {i, t, p.material, uint32_t(w.materials[p.material].doubleSided)};
            result.lights.push_back(light); weights.push_back(area * power[p.material]); total += weights.back();
        }
    }
    if (!(total > 0) || !std::isfinite(total)) {
        for (auto &i : instances) i.primitive.y = UINT32_MAX;
        result.lights.clear();
    }
    double cdf = 0;
    float previous = 0;
    for (size_t i = 0; i < result.lights.size(); ++i) {
        cdf += weights[i] / total;
        float end = i + 1 == result.lights.size() ? 1.f : float(cdf);
        result.lights[i].edge1Cdf.w = end;
        // Use the mass of the rounded CDF interval, also used by the GPU selector.
        result.lights[i].edge2Probability.w = end - previous; previous = end;
    }
    result.count = uint32_t(result.lights.size());
    if (result.lights.empty()) result.lights.emplace_back(); // valid descriptor even with no emitters
    return result;
}
} // namespace tutorial
