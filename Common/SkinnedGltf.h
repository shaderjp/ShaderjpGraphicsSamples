#pragma once
#include "Application.h"
#include <DirectXMath.h>
#include <array>
#include <limits>

namespace tutorial
{
// CPU interchange data. No graphics API resources are owned here.
struct alignas(16) SkinVertex
{
    DirectX::XMFLOAT4 positionU{}, normalV{}, tangent{1, 0, 0, -1};
    DirectX::XMUINT4 joints{};
    DirectX::XMFLOAT4 weights{1, 0, 0, 0};
};
struct alignas(16) SkinDeformedVertex
{
    DirectX::XMFLOAT4 positionU{}, normalV{}, tangent{};
};
static_assert(sizeof(SkinVertex) == 80 && sizeof(SkinDeformedVertex) == 48);
struct SkinMaterial
{
    std::string name;
    DirectX::XMFLOAT4 base{1, 1, 1, 1};
    DirectX::XMFLOAT3 emission{};
    float metallic = 1, roughness = 1, cutoff = .5f;
    bool mask = false, doubleSided = false;
};
struct SkinNode
{
    std::string name;
    int parent = -1, mesh = -1, skin = -1;
    DirectX::XMFLOAT3 translation{}, scale{1, 1, 1};
    DirectX::XMFLOAT4 rotation{0, 0, 0, 1};
    DirectX::XMFLOAT4X4 matrix{};
    bool hasMatrix = false;
};
struct SkinDefinition
{
    std::vector<uint32_t> joints;
    std::vector<DirectX::XMFLOAT4X4> inverseBind;
};
struct SkinPrimitive
{
    std::vector<SkinVertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t material = 0, node = 0, skin = 0;
};
enum class SkinChannelPath
{
    Translation,
    Rotation,
    Scale
};
struct SkinChannel
{
    uint32_t node = 0;
    SkinChannelPath path{};
    bool step = false;
    std::vector<float> times;
    std::vector<DirectX::XMFLOAT4> values;
};
struct SkinClip
{
    std::string name;
    float duration = 0;
    std::vector<SkinChannel> channels;
};
struct GltfSkinnedModel
{
    std::vector<SkinMaterial> materials;
    std::vector<SkinPrimitive> primitives;
    std::vector<SkinNode> nodes;
    std::vector<SkinDefinition> skins;
    std::vector<SkinClip> clips;
};
struct SkinPose
{
    std::vector<DirectX::XMFLOAT4X4> globals;
};
inline constexpr size_t SkinRestPose = std::numeric_limits<size_t>::max();
GltfSkinnedModel LoadGltfSkinned(const std::filesystem::path &path);
SkinPose EvaluateGltfPose(const GltfSkinnedModel &model, size_t clip, float seconds);
std::vector<DirectX::XMFLOAT4X4> MakeSkinPalette(const GltfSkinnedModel &, const SkinPose &,
                                                 size_t primitive);
SkinDeformedVertex DeformSkinVertex(const SkinVertex &, const std::vector<DirectX::XMFLOAT4X4> &);
} // namespace tutorial
