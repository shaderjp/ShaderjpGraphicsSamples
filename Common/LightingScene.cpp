#include "LightingScene.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace tutorial
{
LightingScene LoadLightingScene(const std::filesystem::path &path)
{
    try
    {
        const auto bytes = ReadBinary(path);
        const auto j = nlohmann::json::parse(bytes.begin(), bytes.end());
        if (j.at("version") != 1 || j.at("coordinateSystem") != "right-handed Y-up meters")
            throw std::runtime_error("Unsupported lighting scene profile");
        auto vector = [](const nlohmann::json &a) {
            if (!a.is_array() || a.size() != 3)
                throw std::runtime_error("Expected three components");
            DirectX::XMFLOAT3 v{a.at(0).get<float>(), a.at(1).get<float>(), a.at(2).get<float>()};
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                throw std::runtime_error("Non-finite scene vector");
            return v;
        };
        LightingScene s;
        s.camera = vector(j.at("camera").at("position"));
        s.camera.z = -s.camera.z;
        s.target = vector(j.at("camera").at("target"));
        s.target.z = -s.target.z;
        if (j.contains("secondaryCamera"))
        {
            s.secondaryCamera = vector(j.at("secondaryCamera").at("position"));
            s.secondaryCamera.z = -s.secondaryCamera.z;
            s.secondaryTarget = vector(j.at("secondaryCamera").at("target"));
            s.secondaryTarget.z = -s.secondaryTarget.z;
            s.secondaryFov = j.at("secondaryCamera").at("fovDegrees").get<float>();
            if (!(s.secondaryFov > 1 && s.secondaryFov < 170))
                throw std::runtime_error("Invalid secondary camera");
            const auto forward = DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&s.secondaryTarget),
                                                           DirectX::XMLoadFloat3(&s.secondaryCamera));
            if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(forward)) < 1e-8f ||
                DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(
                    DirectX::XMVector3Cross(forward, DirectX::XMVectorSet(0, 1, 0, 0)))) < 1e-8f)
                throw std::runtime_error("Degenerate secondary camera");
        }
        s.direction = vector(j.at("light").at("direction"));
        s.direction.z = -s.direction.z;
        s.lightColor = vector(j.at("light").at("color"));
        s.fov = j.at("camera").at("fovDegrees").get<float>();
        s.intensity = j.at("light").at("intensity").get<float>();
        s.exposure = j.at("exposure").get<float>();
        if (!(s.fov > 1 && s.fov < 170) || !std::isfinite(s.intensity) || s.intensity < 0 ||
            !std::isfinite(s.exposure) || s.exposure <= 0 ||
            DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(DirectX::XMLoadFloat3(&s.direction))) < 1e-8f)
            throw std::runtime_error("Invalid camera/light/exposure");
        if (j.at("objects").empty() || j.at("objects").size() > 128)
            throw std::runtime_error("Invalid object count");
        for (const auto &o : j.at("objects"))
        {
            LightingObject item;
            item.name = o.at("name").get<std::string>();
            const auto utf8 = o.at("model").get<std::string>();
            item.model = LoadGltfPbr(path.parent_path() /
                                     std::filesystem::path(std::u8string(utf8.begin(), utf8.end())));
            item.position = vector(o.at("position"));
            item.position.z = -item.position.z;
            item.rotation = vector(o.at("rotation"));
            item.rotation.x = -item.rotation.x;
            item.rotation.y = -item.rotation.y;
            item.scale = vector(o.at("scale"));
            if (item.scale.x <= 0 || item.scale.y <= 0 || item.scale.z <= 0)
                throw std::runtime_error("Positive scales required");
            item.castsShadow = o.at("castsShadow").get<bool>();
            item.movable = o.at("movable").get<bool>();
            item.renderTexture = o.value("renderTexture", false);
            s.objects.push_back(std::move(item));
        }
        for (size_t i = 0; i < 2; ++i)
            s.environments[i] =
                path.parent_path() / j.at("environments").at(i ? "Room" : "Studio").get<std::string>();
        return s;
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("Scene " + path.string() + ": " + e.what());
    }
}
LabConstants MakeLabConstants(uint32_t width, uint32_t height, const LightingScene &s, size_t object,
                              int technique, int view, float envIntensity, float yaw, float direct,
                              float exposure, bool shadows, bool pcf, float bias, float offset)
{
    using namespace DirectX;
    const auto &o = s.objects.at(object);
    const auto world =
        XMMatrixScaling(o.scale.x, o.scale.y, o.scale.z) *
        XMMatrixRotationRollPitchYaw(XMConvertToRadians(o.rotation.x), XMConvertToRadians(o.rotation.y),
                                     XMConvertToRadians(o.rotation.z)) *
        XMMatrixTranslation(o.position.x + (o.movable ? offset : 0), o.position.y, o.position.z);
    const auto eye = XMLoadFloat3(&s.camera), target = XMLoadFloat3(&s.target);
    const auto vp =
        XMMatrixLookAtLH(eye, target, XMVectorSet(0, 1, 0, 0)) *
        XMMatrixPerspectiveFovLH(XMConvertToRadians(s.fov), float(width) / float(height), .05f, 40);
    const auto light = XMVector3Normalize(XMLoadFloat3(&s.direction));
    const auto lvp = XMMatrixLookAtLH(XMVectorSet(0, .8f, 0, 0) + light * 10, XMVectorSet(0, .8f, 0, 0),
                                      XMVectorSet(0, 1, 0, 0)) *
                     XMMatrixOrthographicLH(11, 11, .1f, 25);
    LabConstants c{};
    XMStoreFloat4x4(&c.lighting.world, world);
    XMStoreFloat4x4(&c.lighting.viewProjection, vp);
    XMStoreFloat4x4(&c.lighting.normalMatrix, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
    XMStoreFloat4(&c.lighting.cameraPosition, eye);
    XMStoreFloat4(&c.lighting.lightDirection, light);
    c.lighting.baseColorFactor = o.model.baseColorFactor;
    c.lighting.lightColor = {s.lightColor.x * direct, s.lightColor.y * direct, s.lightColor.z * direct, 0};
    c.lighting.ambientExposure = {0, 0, 0, exposure};
    c.lighting.material = {o.model.metallicFactor, o.model.roughnessFactor, o.model.normalScale,
                           o.model.occlusionStrength};
    c.lighting.emissiveFactor = {o.model.emissiveFactor.x, o.model.emissiveFactor.y, o.model.emissiveFactor.z,
                                 0};
    XMStoreFloat4x4(&c.inverseViewProjection, XMMatrixInverse(nullptr, vp));
    XMStoreFloat4x4(&c.lightViewProjection, lvp);
    c.environment = {envIntensity, XMConvertToRadians(yaw), float(technique), float(view)};
    c.shadow = {shadows ? 1.f : 0.f, pcf ? 1.f : 0.f, bias, 2048};
    return c;
}
} // namespace tutorial
