#pragma once
#include "Application.h"
#include "DepthMath.h"
#include <DirectXMath.h>
#include <array>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>

namespace tutorial::depth
{
using namespace DirectX;
struct Settings
{
    float nearZ = .1f, farZ = 10000, displayFar = 40, gap = .01f, bias = .04f;
    int format = 2, preset = 0, resolution = 1024, graph = 0;
    bool orthographic = false, infinite = false, spot = false, pcf = true, stress = false;
    int encoding = 2; // 0 projected, 1 explicit linear, 2 compare
    void Preset(int p, bool shadow)
    {
        *this = Settings{};
        preset = p;
        if (shadow)
        {
            farZ = 40;
            spot = p == 1;
            if (p == 2)
                bias = 0;
            if (p == 3)
                resolution = 256;
        }
        else
        {
            if (p == 1)
                nearZ = .001f;
            if (p == 2)
            {
                stress = true;
                displayFar = 1200;
            }
            if (p == 3)
            {
                infinite = true;
                displayFar = 1200;
                stress = true;
            }
            if (p == 4)
            {
                orthographic = true;
                farZ = 40;
            }
        }
    }
    Mapping Map(bool reverse) const
    {
        return {nearZ, farZ, reverse, orthographic, infinite};
    }
    void Parse(const Options &options, bool shadow)
    {
        Preset(0, shadow);
        for (const auto &[key, value] : options.depthOptions)
            if (key == L"--depth-preset")
            {
                const std::vector<std::wstring> names =
                    shadow ? std::vector<std::wstring>{L"directional", L"spot", L"bias", L"resolution"}
                           : std::vector<std::wstring>{L"mapping", L"near", L"precision", L"infinite",
                                                       L"orthographic"};
                auto it = std::find(names.begin(), names.end(), value);
                if (it == names.end())
                    throw std::runtime_error("Unknown depth preset for this sample");
                Preset(int(it - names.begin()), shadow);
            }
        for (const auto &[key, value] : options.depthOptions)
        {
            auto choice = [&](std::initializer_list<const wchar_t *> names)
            {
                int i = 0;
                for (auto name : names)
                {
                    if (value == name)
                        return i;
                    ++i;
                }
                throw std::runtime_error("Invalid depth option: " + std::filesystem::path(key).string());
            };
            auto number = [&]
            {
                size_t used = 0;
                float v = std::stof(value, &used);
                if (used != value.size() || !std::isfinite(v))
                    throw std::runtime_error("Invalid finite depth number");
                return v;
            };
            if (key == L"--depth-preset")
                continue;
            else if (key == L"--depth-format")
                format = choice({L"d16", L"d24", L"d32"});
            else if (key == L"--depth-graph")
                graph = choice({L"mapping", L"spacing", L"toy"});
            else if (key == L"--depth-near")
                nearZ = number();
            else if (key == L"--depth-far")
                farZ = number();
            else if (key == L"--depth-display-far")
                displayFar = number();
            else if (key == L"--depth-gap")
                gap = number();
            else if (key == L"--depth-projection")
            {
                orthographic = choice({L"perspective", L"orthographic"}) != 0;
            }
            else if (key == L"--depth-infinite")
                infinite = choice({L"off", L"on"}) != 0;
            else if (key == L"--depth-light")
            {
                if (!shadow)
                    throw std::runtime_error("Light requires sample 36");
                spot = choice({L"directional", L"spot"}) != 0;
            }
            else if (key == L"--depth-encoding")
            {
                if (!shadow)
                    throw std::runtime_error("Encoding requires sample 36");
                encoding = choice({L"projected", L"linear", L"compare"});
            }
            else if (key == L"--depth-bias")
                bias = number();
            else if (key == L"--depth-resolution")
            {
                float v = number();
                if (v != 256 && v != 1024 && v != 2048)
                    throw std::runtime_error("Shadow resolution: 256 / 1024 / 2048");
                resolution = int(v);
            }
            else if (key == L"--depth-pcf")
                pcf = choice({L"off", L"on"}) != 0;
            else
                throw std::runtime_error("Unknown depth option: " + std::filesystem::path(key).string());
        }
        Map(false).Validate();
        if (nearZ < .0001f || farZ > 1000000 || displayFar <= nearZ || displayFar > 1000000 || gap <= 0 ||
            gap > 10 || bias < 0 || bias > 1 ||
            (resolution != 256 && resolution != 1024 && resolution != 2048))
            throw std::runtime_error("Depth parameter outside teaching range");
        if (shadow && (format != 2 || infinite || orthographic || graph == 2))
            throw std::runtime_error("Sample 36 uses D32, finite light range; choose --depth-light");
    }
};
struct Vertex
{
    XMFLOAT3 position, color;
    float id;
};
static_assert(sizeof(Vertex) == 28);
struct alignas(16) Constants
{
    XMFLOAT4X4 view, projection, lightView, lightProjection;
    XMFLOAT4 range;   // near, far, display far, receiver bias (metres)
    XMFLOAT4 mode;    // sample36, reverse, explicit linear shadow, PCF
    XMFLOAT4 display; // panel, kind (color/raw/linear/export), ortho, infinite
    XMFLOAT4 extent;  // source width, source height, shadow size, spare
};
static_assert(sizeof(Constants) == 320);
inline XMMATRIX Projection(const Mapping &m, float aspect)
{
    if (m.orthographic)
        return XMMatrixOrthographicLH(24 * aspect, 24, float(m.reversed ? m.farZ : m.nearZ),
                                      float(m.reversed ? m.nearZ : m.farZ));
    const float y = 1 / std::tan(XMConvertToRadians(60.f) * .5f), x = y / aspect;
    const float a = m.infinite
                        ? (m.reversed ? 0.f : 1.f)
                        : float(m.reversed ? -m.nearZ / (m.farZ - m.nearZ) : m.farZ / (m.farZ - m.nearZ));
    const float b = m.infinite ? float(m.reversed ? m.nearZ : -m.nearZ)
                               : float((m.reversed ? 1 : -1) * m.nearZ * m.farZ / (m.farZ - m.nearZ));
    return XMMatrixSet(x, 0, 0, 0, 0, y, 0, 0, 0, 0, a, 1, 0, 0, b, 0);
}
inline Constants MakeConstants(const Settings &s, bool shadow, int panel, uint32_t width, uint32_t height)
{
    Constants c{};
    XMMATRIX view = shadow ? XMMatrixLookAtLH(XMVectorSet(10, 9, -13, 1), XMVectorSet(0, 1, 3, 1),
                                              XMVectorSet(0, 1, 0, 0))
                           : XMMatrixIdentity();
    auto m = s.Map(shadow || panel == 1);
    if (shadow)
    {
        m = {.1, 100, true, false, false};
    }
    XMStoreFloat4x4(&c.view, view);
    XMStoreFloat4x4(&c.projection, Projection(m, float(width) / height));
    XMStoreFloat4x4(&c.lightView, XMMatrixLookAtLH(XMVectorSet(-7, 12, -5, 1), XMVectorSet(0, 0, 3, 1),
                                                   XMVectorSet(0, 1, 0, 0)));
    Mapping light{s.nearZ, s.farZ, false, !s.spot, false};
    XMStoreFloat4x4(&c.lightProjection, Projection(light, 1));
    c.range = {s.nearZ, s.farZ, s.displayFar, s.bias};
    c.mode = {shadow ? 1.f : 0.f, shadow || panel == 1 ? 1.f : 0.f,
              float(s.encoding == 2 ? panel : s.encoding), s.pcf ? 1.f : 0.f};
    c.display = {float(panel), 0, shadow ? 0.f : float(s.orthographic), shadow ? 0.f : float(s.infinite)};
    c.extent = {float(width), float(height), float(s.resolution), 0};
    return c;
}
inline std::vector<Vertex> Scene(const Settings &s, bool shadow)
{
    std::vector<Vertex> v;
    auto quad = [&](XMFLOAT3 a, XMFLOAT3 b, XMFLOAT3 c, XMFLOAT3 d, XMFLOAT3 color, float id)
    {
        for (auto p : {a, b, c, a, c, d})
            v.push_back({p, color, id});
    };
    auto box = [&](float x, float y, float z, float sx, float sy, float sz, XMFLOAT3 col, float id)
    {
        XMFLOAT3 a{x - sx, y, z - sz}, b{x + sx, y, z - sz}, c{x + sx, y + sy, z - sz},
            d{x - sx, y + sy, z - sz};
        XMFLOAT3 e{x - sx, y, z + sz}, f{x + sx, y, z + sz}, g{x + sx, y + sy, z + sz},
            h{x - sx, y + sy, z + sz};
        quad(a, b, c, d, col, id);
        quad(f, e, h, g, {col.x * .65f, col.y * .65f, col.z * .65f}, id);
        quad(d, c, g, h, {col.x * 1.15f, col.y * 1.15f, col.z * 1.15f}, id);
        quad(b, f, g, c, {col.x * .8f, col.y * .8f, col.z * .8f}, id);
        quad(e, a, d, h, col, id);
    };
    if (shadow)
    {
        for (int z = -5; z < 13; ++z)
            for (int x = -9; x < 10; ++x)
            {
                float t = ((x + z) & 1) ? .2f : .3f;
                quad({float(x), 0, float(z)}, {float(x + 1), 0, float(z)}, {float(x + 1), 0, float(z + 1)},
                     {float(x), 0, float(z + 1)}, {t, t * 1.15f, t * 1.3f}, 1);
            }
        box(-2, 0, 2, 1, 3, 1, {.08f, .55f, .8f}, 2);
        box(2, 0, 5, 1.4f, 2, 1, {.95f, .35f, .08f}, 3);
        box(0, 0, 9, 1, 4, .8f, {.3f, .75f, .45f}, 4);
        quad({3, 0, 0}, {6, 0, 0}, {6, 3, 6}, {3, 3, 6}, {.6f, .4f, .65f}, 5);
    }
    else if (s.stress)
    {
        // Far surface first. A physically nearer surface must win regardless of draw order.
        // Parallel sloped planes exercise many depths, rather than one lucky rounding boundary.
        quad({-450, -210, 910 + s.gap}, {450, -210, 1090 + s.gap}, {450, 210, 1090 + s.gap},
             {-450, 210, 910 + s.gap}, {.85f, .22f, .06f}, 10);
        quad({-400, -180, 920}, {400, -180, 1080}, {400, 180, 1080}, {-400, 180, 920}, {.04f, .6f, .85f}, 11);
    }
    else
    {
        for (int z = 1; z < 40; ++z)
        {
            float t = z % 2 ? .14f : .23f;
            quad({-18, -2, float(z)}, {18, -2, float(z)}, {18, -2, float(z + 1)}, {-18, -2, float(z + 1)},
                 {t, t * 1.2f, t * 1.5f}, 1);
        }
        box(-1.5f, -2, 5, .6f, 2, .6f, {.06f, .6f, .8f}, 2);
        box(2, -2, 10, 1, 3, 1, {.9f, .3f, .08f}, 3);
        box(-4, -2, 20, 2, 5, 2, {.3f, .75f, .4f}, 4);
        box(7, -2, 35, 2, 7, 2, {.65f, .35f, .8f}, 5);
        quad({-8, -1, 7}, {8, -1, 32}, {8, 0, 32}, {-8, 0, 7}, {.65f, .65f, .55f}, 6);
    }
    return v;
}
struct Rect
{
    float x = 0, y = 0, w = 1, h = 1;
};
struct Pixel
{
    float raw = 0, z = 0, id = 0, coverage = 0;
};
inline void SaveMeasurements(const std::filesystem::path &image, const Settings &s, bool shadow, uint32_t w,
                             uint32_t h, const std::array<std::vector<Pixel>, 2> &pixels,
                             const std::vector<Pixel> &light, const char *api)
{
    auto dir = image.parent_path() / (image.stem().wstring() + L"-gpu");
    std::filesystem::create_directories(dir);
    auto write = [&](const char *name, const std::vector<Pixel> &data)
    {
        std::ofstream f(dir / name, std::ios::binary);
        f.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size() * sizeof(Pixel)));
        if (!f)
            throw std::runtime_error("Depth readback write failed");
    };
    write("standard.bin", pixels[0]);
    write("reversed.bin", pixels[1]);
    if (shadow)
        write("shadow.bin", light);
    nlohmann::json j = {
        {"api", api},
        {"sample", shadow ? 36 : 35},
        {"width", w},
        {"height", h},
        {"format", s.format},
        {"near", s.nearZ},
        {"far", s.farZ},
        {"orthographic", s.orthographic},
        {"infinite", s.infinite},
        {"gap", s.gap},
        {"spot", s.spot},
        {"bias", s.bias},
        {"resolution", s.resolution},
        {"encoding", s.encoding},
        {"pcf", s.pcf},
        {"layout", "little-endian float32 raw,z,id,coverage"},
        {"shadowLayout", "raw projected depth, explicit linear depth, 0, coverage"},
        {"panels", shadow ? "projected / linear (or explicit override)" : "standard / reversed"},
        {"note", "GPU sampled depth; not original packed D16/D24 storage bytes"}};
    std::ofstream f(dir / "metadata.json");
    f << j.dump(2);
    if (!f)
        throw std::runtime_error("Depth metadata write failed");
}
} // namespace tutorial::depth
