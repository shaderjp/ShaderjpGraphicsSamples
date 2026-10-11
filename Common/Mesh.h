#pragma once
#include "Application.h"
#include <DirectXMath.h>
namespace tutorial
{
struct ColorVertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
};
struct ModelVertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT2 uv;
    DirectX::XMFLOAT3 normal;
};
struct Mesh
{
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
};
struct alignas(16) SceneConstants
{
    DirectX::XMFLOAT4X4 mvp;
};
static_assert(sizeof(SceneConstants) == 64);
Mesh LoadObj(const std::filesystem::path &path);
SceneConstants MakeSceneConstants(uint32_t width, uint32_t height);
} // namespace tutorial
