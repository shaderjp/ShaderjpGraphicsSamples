#pragma once
#include "Mesh.h"

namespace tutorial
{
// glTF values stay CPU data; each sample translates them to its graphics API.
struct GltfSampler
{
    uint32_t magFilter = 9729, minFilter = 9987;
    uint32_t wrapS = 10497, wrapT = 10497;
    bool operator==(const GltfSampler &) const = default;
};
struct GltfModel
{
    Mesh mesh;
    std::vector<DirectX::XMFLOAT4> tangents;
    std::filesystem::path albedoPath;
    DirectX::XMFLOAT4 baseColorFactor{1, 1, 1, 1};
    GltfSampler sampler;
    // Populated only by LoadGltfPbr; Sample 06/07 keep the Albedo-only path.
    std::filesystem::path normalPath, ormPath, emissivePath;
    float metallicFactor = 1, roughnessFactor = 1, normalScale = 1, occlusionStrength = 1;
    DirectX::XMFLOAT3 emissiveFactor{0, 0, 0};
    DirectX::XMFLOAT3 minimum{}, maximum{};
};
struct alignas(16) GltfSceneConstants
{
    DirectX::XMFLOAT4X4 mvp;
    DirectX::XMFLOAT4 baseColorFactor;
};
static_assert(sizeof(GltfSceneConstants) == 80);

// A deliberately restricted glTF 2.0 profile, not a general-purpose importer.
// Parses only Base Color material inputs; other PBR textures are not loaded.
GltfModel LoadGltfAlbedo(const std::filesystem::path &path);
// Tutorial PBR profile: four PNGs, shared sampler, packed ORM, explicit tangents.
GltfModel LoadGltfPbr(const std::filesystem::path &path);
GltfSceneConstants MakeGltfSceneConstants(uint32_t width, uint32_t height, const GltfModel &model);
} // namespace tutorial
