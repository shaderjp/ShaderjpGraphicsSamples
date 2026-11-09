#pragma once
#include "DeferredData.h"
#include <chrono>
#include <limits>

namespace tutorial
{
// CPU geometry and visibility only. GPU resources and synchronization live in each Sample.
enum class CullMode
{
    Off,
    Frustum,
    Depth,
    Query,
    Software,
    HiZ
};
inline constexpr const char *CullNames[] = {"off", "frustum", "depth-only", "query", "software", "hiz"};
struct CullBounds
{
    DirectX::XMFLOAT4 minimum{}, maximum{};
};
struct CullProjection
{
    float left = 0, top = 0, right = 0, bottom = 0, nearest = 0;
    bool safe = false;
};
struct alignas(16) CullConstants
{
    DirectX::XMFLOAT4X4 viewProjection{};
    DirectX::XMUINT4 dimensions{}; // real width,height; padded width,height
    DirectX::XMUINT4 level{};      // source offset,destination offset,source width,source height
    DirectX::XMUINT4 info{};       // object count,operation,debug mip,unused
};
static_assert(sizeof(CullBounds) == 32 && sizeof(CullConstants) == 112);
inline uint32_t CullPowerOfTwo(uint32_t n)
{
    uint32_t p = 1;
    while (p < n)
        p *= 2;
    return p;
}
inline std::array<DirectX::XMFLOAT4, 6> CullPlanes(const DirectX::XMFLOAT4X4 &m)
{
    using namespace DirectX;
    XMVECTOR x = XMVectorSet(m._11, m._21, m._31, m._41), y = XMVectorSet(m._12, m._22, m._32, m._42),
             z = XMVectorSet(m._13, m._23, m._33, m._43), w = XMVectorSet(m._14, m._24, m._34, m._44);
    XMVECTOR values[] = {w + x, w - x, w + y, w - y, z, w - z};
    std::array<XMFLOAT4, 6> planes{};
    for (int i = 0; i < 6; ++i)
        XMStoreFloat4(&planes[i], XMPlaneNormalize(values[i]));
    return planes;
}
// 0 = outside, 1 = intersecting, 2 = entirely inside.
inline uint32_t CullFrustum(const CullBounds &b, const std::array<DirectX::XMFLOAT4, 6> &planes)
{
    uint32_t result = 2;
    float cx = (b.minimum.x + b.maximum.x) * .5f, cy = (b.minimum.y + b.maximum.y) * .5f,
          cz = (b.minimum.z + b.maximum.z) * .5f;
    float ex = (b.maximum.x - b.minimum.x) * .5f, ey = (b.maximum.y - b.minimum.y) * .5f,
          ez = (b.maximum.z - b.minimum.z) * .5f;
    for (const auto &p : planes)
    {
        float d = p.x * cx + p.y * cy + p.z * cz + p.w,
              r = std::abs(p.x) * ex + std::abs(p.y) * ey + std::abs(p.z) * ez;
        if (d + r < -1e-5f)
            return 0;
        if (d - r < 1e-5f)
            result = 1;
    }
    return result;
}
inline DirectX::XMVECTOR CullCorner(const CullBounds &b, int c)
{
    return DirectX::XMVectorSet(c & 1 ? b.maximum.x : b.minimum.x, c & 2 ? b.maximum.y : b.minimum.y,
                                c & 4 ? b.maximum.z : b.minimum.z, 1);
}
inline CullProjection ProjectCullBounds(const CullBounds &b, const DirectX::XMFLOAT4X4 &vp, uint32_t width,
                                        uint32_t height)
{
    using namespace DirectX;
    CullProjection r{float(width), float(height), 0, 0, 1, true};
    for (int c = 0; c < 8; ++c)
    {
        XMFLOAT4 p;
        XMStoreFloat4(&p, XMVector4Transform(CullCorner(b, c), XMLoadFloat4x4(&vp)));
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(p.w) ||
            p.w <= 1e-5f || p.z <= 1e-5f)
        {
            r.safe = false;
            return r;
        }
        float x = (p.x / p.w * .5f + .5f) * width, y = (.5f - p.y / p.w * .5f) * height;
        r.left = std::min(r.left, x);
        r.right = std::max(r.right, x);
        r.top = std::min(r.top, y);
        r.bottom = std::max(r.bottom, y);
        r.nearest = std::min(r.nearest, p.z / p.w);
    }
    r.left = std::clamp(std::floor(r.left) - 1, 0.f, float(width));
    r.top = std::clamp(std::floor(r.top) - 1, 0.f, float(height));
    r.right = std::clamp(std::ceil(r.right) + 1, 0.f, float(width));
    r.bottom = std::clamp(std::ceil(r.bottom) + 1, 0.f, float(height));
    r.safe = r.right > r.left && r.bottom > r.top;
    return r;
}
struct SoftwareDepth
{
    uint32_t width = 0, height = 0;
    std::vector<float> pixels;
    void Reset(uint32_t w, uint32_t h)
    {
        width = w;
        height = h;
        pixels.assign(size_t(w) * h, 1);
    }
    void Triangle(DirectX::XMFLOAT4 a, DirectX::XMFLOAT4 b, DirectX::XMFLOAT4 c, bool twoSided = false)
    {
        // Homogeneous clipping before division. Every new polygon is still inside the real occluder.
        std::vector<DirectX::XMFLOAT4> polygon{a, b, c};
        for (int plane = 0; plane < 6; ++plane)
        {
            auto distance = [&](const DirectX::XMFLOAT4 &v)
            {
                switch (plane)
                {
                case 0:
                    return v.w + v.x;
                case 1:
                    return v.w - v.x;
                case 2:
                    return v.w + v.y;
                case 3:
                    return v.w - v.y;
                case 4:
                    return v.z;
                default:
                    return v.w - v.z;
                }
            };
            std::vector<DirectX::XMFLOAT4> next;
            for (size_t i = 0; i < polygon.size(); ++i)
            {
                auto p = polygon[i], q = polygon[(i + 1) % polygon.size()];
                float d = distance(p), e = distance(q);
                if (d >= 0)
                    next.push_back(p);
                if ((d >= 0) != (e >= 0))
                {
                    float t = d / (d - e);
                    next.push_back({p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t,
                                    p.w + (q.w - p.w) * t});
                }
            }
            polygon = std::move(next);
            if (polygon.size() < 3)
                return;
        }
        for (auto &p : polygon)
        {
            if (p.w <= 1e-6f)
                return;
            p = {(p.x / p.w * .5f + .5f) * width, (.5f - p.y / p.w * .5f) * height, p.z / p.w, 1};
        }
        auto edge = [](const auto &p, const auto &q, float x, float y)
        { return (q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x); };
        for (size_t t = 1; t + 1 < polygon.size(); ++t)
        {
            a = polygon[0];
            b = polygon[t];
            c = polygon[t + 1];
            // Clockwise is front-facing after the viewport's Y flip, exactly as in both GPU pipelines.
            // A back face exposed by near clipping cannot become an occluder on the CPU alone.
            if (edge(a, b, c.x, c.y) <= 0 && !twoSided)
                continue;
            if (edge(a, b, c.x, c.y) < 0)
                std::swap(b, c);
            float area = edge(a, b, c.x, c.y);
            if (area < 1e-6f)
                continue;
            int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x})))),
                x1 = std::min(int(width), int(std::ceil(std::max({a.x, b.x, c.x}))));
            int y0 = std::max(0, int(std::floor(std::min({a.y, b.y, c.y})))),
                y1 = std::min(int(height), int(std::ceil(std::max({a.y, b.y, c.y}))));
            // Farthest triangle depth is an upper bound throughout every covered cell.
            float farthest = std::min(1.f, std::max({a.z, b.z, c.z}) + 1e-5f);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                {
                    bool covered = true;
                    for (int corner = 0; corner < 4; ++corner)
                    {
                        float px = float(x + (corner & 1)), py = float(y + ((corner >> 1) & 1));
                        if (edge(a, b, px, py) <= .001f || edge(b, c, px, py) <= .001f ||
                            edge(c, a, px, py) <= .001f)
                        {
                            covered = false;
                            break;
                        }
                    }
                    if (covered)
                    {
                        auto &z = pixels[size_t(y) * width + x];
                        z = std::min(z, farthest);
                    }
                }
        }
    }
    bool Hidden(const CullProjection &p, uint32_t fullWidth, uint32_t fullHeight) const
    {
        if (!p.safe)
            return false;
        int x0 = std::max(0, int(std::floor(p.left * width / fullWidth))),
            x1 = std::min(int(width), int(std::ceil(p.right * width / fullWidth)));
        int y0 = std::max(0, int(std::floor(p.top * height / fullHeight))),
            y1 = std::min(int(height), int(std::ceil(p.bottom * height / fullHeight)));
        if (x1 <= x0 || y1 <= y0)
            return false;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                if (p.nearest <= pixels[size_t(y) * width + x] + 1e-5f)
                    return false;
        return true;
    }
};
struct CullingState
{
    CullMode mode = CullMode::Frustum;
    int diagnostic = 0, divisor = 4, mip = 0;
    bool freeze = false, frozen = false;
    DirectX::XMFLOAT4X4 viewProjection{}, observer{};
    std::vector<CullBounds> bounds;
    std::vector<CullProjection> projected;
    std::vector<uint32_t> classification, visible;
    SoftwareDepth software;
    uint32_t total = 0, frustumRejected = 0, occluded = 0, recorded = 0, gpuObjects = 0;
    double cpuMs = 0, recordMs = 0;
    double gpuColorMs = 0, renderWallMs = 0;
    float doorOffset = 0, targetOffset = 0;
    std::vector<std::array<double, 9>> measurements;
    std::array<double, 4> gpuMs{}; // depth, query, pyramid, cull
    bool UsesDepth() const
    {
        return mode == CullMode::Depth || mode == CullMode::Query || mode == CullMode::HiZ;
    }
    bool Conditional() const
    {
        return mode == CullMode::Query || mode == CullMode::HiZ;
    }
    bool Occluder(size_t i) const
    {
        return bounds[i].minimum.w != 0;
    }
    bool Candidate(size_t i) const
    {
        return mode == CullMode::Off || classification[i] != 0;
    }
    void Record(uint32_t frame, bool benchmark, double gpuTotal)
    {
        if (benchmark && frame > 60 && frame <= 1260)
            measurements.push_back({cpuMs, recordMs, gpuMs[0], gpuMs[1], gpuMs[2], gpuMs[3], gpuColorMs,
                                    gpuTotal, renderWallMs});
    }
    void SaveTiming(const std::filesystem::path &path, uint32_t width, uint32_t height) const
    {
        if (path.empty())
            return;
        nlohmann::json j;
        std::ifstream input(path);
        input >> j;
        input.close();
        j["width"] = width;
        j["height"] = height;
        j["representation"] = "Forward Indexed";
        j["mode"] = CullNames[int(mode)];
        j["pointLights"] = 0;
        j["gBufferBytesPerPixel"] = 0;
        const char *names[] = {"cpuCull", "cpuRecord", "depth",    "queryResolve", "hizBuild",
                               "hizTest", "color",     "gpuTotal", "renderWall"};
        for (size_t i = 0; i < 9; ++i)
        {
            std::vector<double> values;
            for (const auto &m : measurements)
                values.push_back(m[i]);
            std::sort(values.begin(), values.end());
            if (!values.empty())
                j["cullingMs"][names[i]] = {
                    {"median", values[values.size() / 2]},
                    {"p95", values[std::min(values.size() - 1, size_t(values.size() * .95))]}};
        }
        j["cullingSamples"] = measurements;
        j["cullingSampleColumns"] = names;
        j["cpuDepthDivisor"] = divisor;
        std::ofstream(path) << j.dump(2) << '\n';
    }
    void Prepare(const Workshop &w, int scene, const DeferredConstants &frame, float door, float target,
                 uint32_t width, uint32_t height)
    {
        using namespace DirectX;
        auto start = std::chrono::steady_clock::now();
        doorOffset = door;
        targetOffset = target;
        observer = frame.viewProjection;
        if (!freeze || !frozen)
            viewProjection = observer;
        frozen = freeze;
        auto planes = CullPlanes(viewProjection);
        const auto &objects = w.scenes[scene].instances;
        total = uint32_t(objects.size());
        frustumRejected = occluded = recorded = gpuObjects = 0;
        bounds.resize(total);
        projected.resize(total);
        classification.resize(total);
        visible.assign(total, 1);
        for (uint32_t i = 0; i < total; ++i)
        {
            const auto &o = objects[i];
            const auto &p = w.primitives[o.primitive];
            auto world = XMLoadFloat4x4(&o.world);
            if (o.movable)
                world *= XMMatrixTranslation(door, 0, 0);
            if (o.id.starts_with("object:") && o.id.ends_with(":0000"))
                world *= XMMatrixTranslation(target, 0, 0);
            XMVECTOR lo = XMVectorReplicate(FLT_MAX), hi = XMVectorReplicate(-FLT_MAX);
            CullBounds local{{p.minimum.x, p.minimum.y, p.minimum.z, 0},
                             {p.maximum.x, p.maximum.y, p.maximum.z, 0}};
            for (int k = 0; k < 8; ++k)
            {
                auto v = XMVector3TransformCoord(CullCorner(local, k), world);
                lo = XMVectorMin(lo, v);
                hi = XMVectorMax(hi, v);
            }
            XMStoreFloat4(&bounds[i].minimum, lo);
            XMStoreFloat4(&bounds[i].maximum, hi);
            bounds[i].minimum.w = float((o.id.starts_with("occluder:") || o.id.starts_with("door:")) &&
                                        w.materials[p.material].alpha == AlphaMode::Opaque);
            classification[i] = CullFrustum(bounds[i], planes);
            bounds[i].maximum.w = float(classification[i] != 0);
            projected[i] = ProjectCullBounds(bounds[i], viewProjection, width, height);
            if (mode != CullMode::Off && classification[i] == 0)
            {
                visible[i] = 0;
                ++frustumRejected;
            }
        }
        if (mode == CullMode::Software)
        {
            software.Reset(std::max(1u, (width + divisor - 1) / divisor),
                           std::max(1u, (height + divisor - 1) / divisor));
            for (uint32_t i = 0; i < total; ++i)
                if (Candidate(i) && Occluder(i))
                {
                    const auto &o = objects[i];
                    const auto &p = w.primitives[o.primitive];
                    auto world = XMLoadFloat4x4(&o.world);
                    if (o.movable)
                        world *= XMMatrixTranslation(door, 0, 0);
                    auto m = world * XMLoadFloat4x4(&viewProjection);
                    for (size_t t = 0; t < p.indices.size(); t += 3)
                    {
                        XMFLOAT4 v[3];
                        for (int k = 0; k < 3; ++k)
                            XMStoreFloat4(
                                &v[k],
                                XMVector3Transform(XMLoadFloat3(&p.vertices[p.indices[t + k]].position), m));
                        software.Triangle(v[0], v[1], v[2], w.materials[p.material].doubleSided);
                    }
                }
            for (uint32_t i = 0; i < total; ++i)
                if (Candidate(i) && !Occluder(i) && software.Hidden(projected[i], width, height))
                {
                    visible[i] = 0;
                    ++occluded;
                }
        }
        gpuObjects = total - frustumRejected - occluded;
        cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
    void Results(const uint64_t *flags)
    {
        occluded = 0;
        for (uint32_t i = 0; i < total; ++i)
        {
            visible[i] = Candidate(i) && (Occluder(i) || !projected[i].safe || flags[i] != 0);
            if (Candidate(i) && !visible[i])
                ++occluded;
        }
        gpuObjects = total - frustumRejected - occluded;
    }
    void Save(const std::filesystem::path &path, const Workshop &w, int scene) const
    {
        nlohmann::json j = {{"mode", CullNames[int(mode)]},
                            {"total", total},
                            {"frustumRejected", frustumRejected},
                            {"occluded", occluded},
                            {"recordedDraws", recorded},
                            {"gpuObjects", gpuObjects},
                            {"cpuCullMs", cpuMs},
                            {"cpuRecordMs", recordMs},
                            {"gpuPassMs", gpuMs},
                            {"frozen", freeze},
                            {"visible", visible},
                            {"classification", classification}};
        j["doorX"] = doorOffset;
        j["objectX"] = targetOffset;
        for (const auto &o : w.scenes[scene].instances)
            j["instanceIds"].push_back(o.id);
        std::ofstream(path) << j.dump(2) << '\n';
    }
};
inline Workshop LoadCullingLab(const std::filesystem::path &settings)
{
    auto w = LoadWorkshop(settings);
    for (auto &s : w.scenes)
    {
        if (s.instances.size() > 4598)
            throw std::runtime_error("Culling tutorial supports at most 4598 primitive instances");
        for (auto &o : s.instances)
            o.movable = o.id.starts_with("door:");
    }
    return w;
}
} // namespace tutorial
