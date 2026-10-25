#pragma once
#include "Image.h"
#include "Lighting.h"
#include <array>
namespace tutorial
{
enum class AlphaMode
{
    Opaque,
    Mask,
    Blend
};
enum class ImageSemantic
{
    Color,
    Normal,
    Linear
};
struct WorkshopImage
{
    std::filesystem::path path;
    ImageSemantic semantic{};
    float alphaCutoff = 0;
};
struct WorkshopMaterial
{
    std::string id;
    std::array<uint32_t, 4> images{}; // Base, normal, ORM, emissive
    GltfSampler sampler;
    DirectX::XMFLOAT4 base{1, 1, 1, 1}, factors{1, 1, 1, 1}, emission{};
    AlphaMode alpha = AlphaMode::Opaque;
    float cutoff = .5f;
    bool doubleSided = false;
    // Loaded only by the restricted BSDF tutorial profile; legacy GPU layouts stay unchanged.
    float transmission = 0, ior = 1.5f, thickness = 0, attenuationDistance = 0;
    DirectX::XMFLOAT3 attenuationColor{1, 1, 1}; // distance=0 means no absorption
};
struct WorkshopPrimitive
{
    std::string id;
    std::vector<PbrVertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t material = 0;
    DirectX::XMFLOAT3 minimum{}, maximum{};
};
struct WorkshopInstance
{
    std::string id;
    uint32_t primitive = 0;
    DirectX::XMFLOAT4X4 world{};
    DirectX::XMFLOAT3 minimum{}, maximum{};
    bool movable = false;
};
struct WorkshopLight
{
    DirectX::XMFLOAT4 positionRange{}, colorIntensity{}; // cd / 683; range=0 means infinite
};
struct WorkshopScene
{
    std::string name;
    std::vector<WorkshopInstance> instances;
    std::vector<WorkshopLight> lights;
    DirectX::XMFLOAT3 minimum{}, maximum{}, direction{0, 1, 0};
    DirectX::XMFLOAT4 sun{1, 1, 1, 1}; // lux / 683
};
struct WorkshopCamera
{
    std::string name;
    DirectX::XMFLOAT3 position{}, target{};
    float fov = 45, nearPlane = .05f, farPlane = 150;
};
struct Workshop
{
    std::vector<WorkshopPrimitive> primitives;
    std::vector<WorkshopMaterial> materials;
    std::vector<WorkshopImage> images;
    std::vector<WorkshopScene> scenes;
    std::vector<WorkshopCamera> cameras, path;
    std::filesystem::path environment;
    float exposure = 1, environmentIntensity = .8f, environmentYaw = 0;
};
Workshop LoadWorkshop(const std::filesystem::path &settings);
// Static path-tracing profile: sun, environment and camera path are optional.
Workshop LoadPathTracingScene(const std::filesystem::path &settings);
Workshop LoadBSDFScene(const std::filesystem::path &settings);
std::vector<Image> GenerateMipChain(Image base, ImageSemantic semantic, float alphaCutoff = 0);
DirectX::XMFLOAT2 EncodeOctahedral(DirectX::XMFLOAT3 normal);
DirectX::XMFLOAT3 DecodeOctahedral(DirectX::XMFLOAT2 encoded);
} // namespace tutorial
