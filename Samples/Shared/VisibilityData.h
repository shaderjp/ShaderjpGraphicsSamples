#pragma once
#include "DeferredData.h"
namespace tutorial
{
inline constexpr uint32_t VisibilityTextureCount = 64;
struct VisibilityMaterial
{
    DirectX::XMFLOAT4 base, factors, emission;
    std::array<uint32_t, 4> images;
    DirectX::XMFLOAT4 alpha;
};
struct VisibilityInstance
{
    DirectX::XMFLOAT4X4 world, normalMatrix;
    std::array<uint32_t, 4> geometry; // vertex offset, index offset, material, triangle count
};
static_assert(sizeof(PbrVertex) == 48);
static_assert(sizeof(VisibilityMaterial) == 80);
static_assert(sizeof(VisibilityInstance) == 144);
struct VisibilityData
{
    std::vector<PbrVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<std::array<uint32_t, 4>> primitives;
    std::vector<VisibilityMaterial> materials;
    explicit VisibilityData(const Workshop &w)
    {
        if (w.images.empty() || w.images.size() > VisibilityTextureCount)
            throw std::runtime_error("Visibility Buffer supports 1..64 unique images");
        for (const auto &s : w.scenes)
            if (s.instances.size() > 1809)
                throw std::runtime_error("Visibility Buffer scene exceeds the 1809 draw-constant slots");
        for (const auto &p : w.primitives)
        {
            if (p.indices.size() / 3 >= 0x80000000ull)
                throw std::runtime_error("Triangle ID exceeds the 31-bit visibility field");
            primitives.push_back({uint32_t(vertices.size()), uint32_t(indices.size()), p.material,
                                  uint32_t(p.indices.size() / 3)});
            vertices.insert(vertices.end(), p.vertices.begin(), p.vertices.end());
            indices.insert(indices.end(), p.indices.begin(), p.indices.end());
        }
        for (const auto &m : w.materials)
            materials.push_back({m.base,
                                 m.factors,
                                 m.emission,
                                 m.images,
                                 {m.cutoff, float(m.alpha == AlphaMode::Mask), float(m.doubleSided), 0}});
    }
};
} // namespace tutorial
