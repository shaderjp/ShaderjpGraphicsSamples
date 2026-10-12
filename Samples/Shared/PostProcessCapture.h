#pragma once
#include "PostProcessData.h"
#include <DirectXPackedVector.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
namespace tutorial
{
inline void CaptureBytes(const std::filesystem::path &path, const void *data, size_t bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(static_cast<const char *>(data), std::streamsize(bytes));
    if (!file)
        throw std::runtime_error("GPU capture write: " + path.string());
}
inline nlohmann::json CaptureHalfImage(const std::filesystem::path &dir, const char *name, uint32_t width,
                                       uint32_t height, const std::vector<uint8_t> &bytes)
{
    if (bytes.size() != size_t(width) * height * 8)
        throw std::runtime_error("HDR capture extent mismatch");
    float low = std::numeric_limits<float>::max(), high = 0;
    for (size_t i = 0; i < bytes.size() / 2; ++i)
    {
        uint16_t bits;
        std::memcpy(&bits, bytes.data() + i * 2, 2);
        float f = DirectX::PackedVector::XMConvertHalfToFloat(bits);
        if (!std::isfinite(f))
            throw std::runtime_error("GPU HDR contains NaN/Inf");
        if (i % 4 < 3)
        {
            low = std::min(low, f);
            high = std::max(high, f);
        }
    }
    CaptureBytes(dir / (std::string(name) + ".rgba16f"), bytes.data(), bytes.size());
    return {{"name", name}, {"width", width}, {"height", height},
            {"min", low},   {"max", high},    {"finite", true}};
}
inline void CaptureParticles(const std::filesystem::path &dir, const void *data, uint32_t active,
                             uint32_t steps, nlohmann::json &metadata)
{
    const auto *p = static_cast<const Particle *>(data);
    for (uint32_t i = 0; i < 65536; ++i)
    {
        std::array<float, 12> v;
        std::memcpy(v.data(), p + i, sizeof(Particle));
        for (float f : v)
            if (!std::isfinite(f))
                throw std::runtime_error("GPU particle contains NaN/Inf");
    }
    CaptureBytes(dir / "particles.bin", data, 65536 * sizeof(Particle));
    metadata["particles"] = {{"active", active}, {"capacity", 65536},  {"stride", sizeof(Particle)},
                             {"steps", steps},   {"sameBuffer", true}, {"finite", true}};
}
inline void CaptureMetadata(const std::filesystem::path &dir, const nlohmann::json &metadata)
{
    auto text = metadata.dump(2) + "\n";
    CaptureBytes(dir / "metadata.json", text.data(), text.size());
}
} // namespace tutorial
