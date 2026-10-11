#include "Gltf.h"
#include "Lighting.h"
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace tutorial;
using Json = nlohmann::json;
namespace
{
void Require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void Write(const std::filesystem::path &path, const Json &data)
{
    std::ofstream stream(path);
    stream << data.dump(2);
}
void WriteBytes(const std::filesystem::path &path, const std::vector<uint8_t> &bytes)
{
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}
void Reject(const std::filesystem::path &path, const Json &data, const char *pointer, bool pbr = false)
{
    Write(path, data);
    try
    {
        if (pbr)
            LoadGltfPbr(path);
        else
            LoadGltfAlbedo(path);
    }
    catch (const std::exception &e)
    {
        const std::string message(e.what());
        Require(message.find("fixture.gltf:") != std::string::npos, "glTF error must identify filename");
        Require(message.find(pointer) != std::string::npos, "glTF error must identify JSON pointer");
        return;
    }
    throw std::runtime_error("Invalid/unsupported glTF unexpectedly accepted");
}
} // namespace

void TestPbrMaterials(const std::filesystem::path &root, const std::filesystem::path &directory)
{
    const auto source = root / "Assets/Models/ShaderBall";
    const auto model = LoadGltfPbr(source / "ShaderBall_PBR.gltf");
    Require(model.normalPath.filename() == "Normal.png" && model.ormPath.filename() == "ORM.png" &&
                model.emissivePath.filename() == "Emissive.png",
            "PBR texture graph references");
    Require(model.metallicFactor == 1 && model.roughnessFactor == 1 && model.normalScale == 1 &&
                model.occlusionStrength == 1 && model.emissiveFactor.x == 1,
            "PBR factors/defaults");
    const auto vertices = MakePbrVertices(model);
    Require(vertices.size() == 4696 && vertices[0].tangent.w == model.tangents[0].w,
            "Packed 48-byte PBR vertices preserve converted tangent");
    const auto light = MakeLightingConstants(800, 600, model, 0, 0);
    const auto resized = MakeLightingConstants(600, 800, model, 1, 1);
    Require(light.shading.x == 0 && resized.shading.x == 1 &&
                light.cameraPosition.z != resized.cameraPosition.z &&
                light.lightDirection.x != resized.lightDirection.x,
            "Lighting camera adapts to portrait viewport; light direction rotates");
    const auto fixtures = directory / "pbr";
    std::filesystem::create_directories(fixtures / "Textures");
    const auto fixture = fixtures / "fixture.gltf";
    const auto bin = fixtures / "ShaderBall_PBR.bin";
    std::filesystem::copy_file(source / "ShaderBall_PBR.bin", bin,
                               std::filesystem::copy_options::overwrite_existing);
    for (const auto *name : {"Albedo", "Normal", "ORM", "Emissive"})
        std::filesystem::copy_file(source / "Textures" / (std::string(name) + ".png"),
                                   fixtures / "Textures" / (std::string(name) + ".png"),
                                   std::filesystem::copy_options::overwrite_existing);
    const auto original = Json::parse(ReadBinary(source / "ShaderBall_PBR.gltf"));
    auto factors = original;
    auto &material = factors["materials"][0];
    material["pbrMetallicRoughness"]["metallicFactor"] = 0.4;
    material["pbrMetallicRoughness"]["roughnessFactor"] = 0.6;
    material["normalTexture"]["scale"] = -2.0; // The glTF schema permits any finite scale.
    material["occlusionTexture"]["strength"] = 0.25;
    material["emissiveFactor"] = {0.2, 0.3, 0.4};
    Write(fixture, factors);
    const auto changed = LoadGltfPbr(fixture);
    Require(changed.metallicFactor == 0.4f && changed.roughnessFactor == 0.6f && changed.normalScale == -2 &&
                changed.occlusionStrength == 0.25f && changed.emissiveFactor.z == 0.4f,
            "PBR material factors");
    const auto baseOnly = LoadGltfAlbedo(fixture);
    Require(baseOnly.normalPath.empty() && baseOnly.ormPath.empty() && baseOnly.emissivePath.empty(),
            "Sample 06/07 must retain their Albedo-only material path");
    auto bad = original;
    bad["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"] = -0.1;
    Reject(fixture, bad, "/roughnessFactor", true);
    bad = original;
    bad["materials"][0]["occlusionTexture"]["strength"] = 2;
    Reject(fixture, bad, "/occlusionTexture/strength", true);
    bad = original;
    bad["materials"][0]["emissiveFactor"] = {1, 1};
    Reject(fixture, bad, "/emissiveFactor", true);
    bad = original;
    bad["materials"][0]["normalTexture"]["texCoord"] = 1;
    Reject(fixture, bad, "/normalTexture/texCoord", true);
    bad = original;
    bad["materials"][0]["occlusionTexture"]["index"] = 3;
    Reject(fixture, bad, "/occlusionTexture", true);
    bad = original;
    bad["samplers"].push_back({{"wrapS", 33071}});
    bad["textures"][1]["sampler"] = 1;
    Reject(fixture, bad, "/normalTexture", true);
    bad = original;
    bad["meshes"][0]["primitives"][0]["attributes"].erase("TANGENT");
    Reject(fixture, bad, "/attributes/TANGENT", true);
    bad = original;
    bad["images"][1]["uri"] = "Textures/MissingNormal.png";
    Reject(fixture, bad, "/images/1/uri", true);
    auto bytes = ReadBinary(bin);
    const auto tangentOffset = original["bufferViews"][3]["byteOffset"].get<size_t>();
    const auto signOffset = tangentOffset + model.mesh.indices[0] * 16 + 12;
    float sign;
    std::memcpy(&sign, bytes.data() + signOffset, sizeof(sign));
    sign = -sign;
    std::memcpy(bytes.data() + signOffset, &sign, sizeof(sign));
    WriteBytes(bin, bytes);
    Reject(fixture, original, "/attributes/TANGENT", true);
    std::cout << "PBR texture/factor/profile/tangent and lighting constants tests passed\n";
}

void TestGltfLoaders(const std::filesystem::path &root, const std::filesystem::path &directory)
{
    TestPbrMaterials(root, directory);
    const auto bundled = LoadGltfAlbedo(root / "Assets/Models/ShaderBall/ShaderBall_Albedo.gltf");
    Require(bundled.mesh.vertices.size() == 4696 && bundled.mesh.indices.size() == 25920,
            "Bundled Shader Ball counts");
    Require(bundled.tangents.empty() && bundled.baseColorFactor.x == 1 && bundled.sampler.wrapS == 10497,
            "glTF defaults and albedo material");
    Require(std::abs(bundled.maximum.y - 1.16f) < 1e-5f && bundled.minimum.y == 0, "glTF Y-up bounds");
    const auto pbr = LoadGltfAlbedo(root / "Assets/Models/ShaderBall/ShaderBall_PBR.gltf");
    Require(pbr.mesh.indices == bundled.mesh.indices && pbr.tangents.size() == 4696,
            "PBR file also loads base color and retains tangents");
    for (size_t i = 0; i < pbr.mesh.vertices.size(); ++i)
        Require(std::memcmp(&pbr.mesh.vertices[i], &bundled.mesh.vertices[i], sizeof(ModelVertex)) == 0,
                "Albedo/PBR geometry agreement");
    const auto constants = MakeGltfSceneConstants(800, 600, bundled);
    Require(constants.baseColorFactor.x == 1 && std::isfinite(constants.mvp._11),
            "glTF camera and constants");

    // Interleaved vertices with nonzero view/accessor offsets and 32-bit indices.
    const auto fixtures = directory / std::filesystem::path(u8"glTF 検証");
    std::filesystem::create_directories(fixtures);
    const auto fixture = fixtures / "fixture.gltf";
    const auto binaryPath = fixtures / "buffer data.bin";
    std::filesystem::copy_file(root / "Assets/Textures/UV.png",
                               fixtures / std::filesystem::path(u8"画像.png"),
                               std::filesystem::copy_options::overwrite_existing);
    std::vector<uint8_t> binary(136, 0);
    const float vertices[3][8] = {
        {0, 0, 1, 0, 0, 1, 0, 0}, {1, 0, 1, 0, 0, 1, 1, 0}, {0, 1, 1, 0, 0, 1, 0, 1}};
    for (size_t i = 0; i < 3; ++i)
        std::memcpy(binary.data() + 20 + 36 * i, vertices[i], 32);
    const uint32_t indexData[3] = {0, 1, 2};
    std::memcpy(binary.data() + 124, indexData, 12);
    WriteBytes(binaryPath, binary);
    Json valid = {
        {"asset", {{"version", "2.0"}}},
        {"scenes", Json::array({{{"nodes", Json::array({0})}}})},
        {"nodes", Json::array({{{"mesh", 0}}})},
        {"meshes",
         Json::array({{{"primitives",
                        Json::array({{{"attributes", {{"POSITION", 0}, {"NORMAL", 1}, {"TEXCOORD_0", 2}}},
                                      {"indices", 3},
                                      {"material", 0}}})}}})},
        {"materials", Json::array({{{"pbrMetallicRoughness",
                                     {{"baseColorTexture", {{"index", 1}}},
                                      {"baseColorFactor", {0.25, 0.5, 0.75, 1.0}}}}}})},
        {"textures", Json::array({{{"source", 0}}, {{"source", 0}}})},
        {"images", Json::array({{{"uri", "%E7%94%BB%E5%83%8F.png"}}})},
        {"buffers", Json::array({{{"uri", "buffer%20data.bin"}, {"byteLength", 136}}})},
        {"bufferViews",
         Json::array({{{"buffer", 0}, {"byteOffset", 16}, {"byteLength", 108}, {"byteStride", 36}},
                      {{"buffer", 0}, {"byteOffset", 124}, {"byteLength", 12}}})},
        {"accessors",
         Json::array(
             {{{"bufferView", 0}, {"byteOffset", 4}, {"componentType", 5126}, {"type", "VEC3"}, {"count", 3}},
              {{"bufferView", 0},
               {"byteOffset", 16},
               {"componentType", 5126},
               {"type", "VEC3"},
               {"count", 3}},
              {{"bufferView", 0},
               {"byteOffset", 28},
               {"componentType", 5126},
               {"type", "VEC2"},
               {"count", 3}},
              {{"bufferView", 1}, {"componentType", 5125}, {"type", "SCALAR"}, {"count", 3}}})}};
    Write(fixture, valid);
    const auto model = LoadGltfAlbedo(fixture);
    Require(model.mesh.indices == std::vector<uint32_t>({0, 2, 1}), "glTF reflection must reverse winding");
    Require(model.mesh.vertices[0].position.z == -1 && model.mesh.vertices[0].normal.z == -1,
            "glTF reflects position/normal Z");
    Require(model.mesh.vertices[0].uv.y == 0 && model.mesh.vertices[2].uv.y == 1, "glTF must not flip UV V");
    Require(model.baseColorFactor.y == 0.5f &&
                model.albedoPath.filename() == std::filesystem::path(u8"画像.png"),
            "Material refs/factor and escaped UTF-8 URI");
    auto altered = valid;
    altered["samplers"] =
        Json::array({{{"magFilter", 9728}, {"minFilter", 9986}, {"wrapS", 33648}, {"wrapT", 33071}}});
    altered["textures"][1]["sampler"] = 0;
    Write(fixture, altered);
    const auto sampled = LoadGltfAlbedo(fixture);
    Require(sampled.sampler.magFilter == 9728 && sampled.sampler.minFilter == 9986 &&
                sampled.sampler.wrapS == 33648 && sampled.sampler.wrapT == 33071,
            "Sampler enums");
    altered["samplers"][0]["magFilter"] = 0;
    Reject(fixture, altered, "/samplers/0/magFilter");
    altered = valid;
    altered["accessors"].push_back(
        {{"bufferView", 0}, {"byteOffset", 4}, {"componentType", 5126}, {"type", "VEC4"}, {"count", 3}});
    altered["meshes"][0]["primitives"][0]["attributes"]["TANGENT"] = 4;
    Reject(fixture, altered, "/accessors/4"); // W reads normal X (=0), not +/-1.

    altered = valid;
    altered["accessors"][0]["byteOffset"] = UINT64_MAX;
    Reject(fixture, altered, "/accessors/0");
    altered = valid;
    altered["accessors"][0]["count"] = UINT64_MAX;
    Reject(fixture, altered, "/accessors/0/count");
    altered = valid;
    altered["bufferViews"][0]["byteLength"] = 100;
    Reject(fixture, altered, "/accessors/2");
    altered = valid;
    altered["bufferViews"][0]["byteStride"] = 8;
    Reject(fixture, altered, "/accessors/0");
    altered = valid;
    altered["accessors"][1]["count"] = 2;
    Reject(fixture, altered, "/attributes");
    altered = valid;
    altered["accessors"][0]["sparse"] = Json::object();
    Reject(fixture, altered, "/accessors/0");
    altered = valid;
    altered["accessors"][0]["normalized"] = true;
    Reject(fixture, altered, "/accessors/0");
    altered = valid;
    altered["accessors"][3]["componentType"] = 5122;
    Reject(fixture, altered, "/accessors/3/componentType");
    altered = valid;
    altered["meshes"][0]["primitives"][0]["attributes"].erase("TEXCOORD_0");
    Reject(fixture, altered, "/attributes/TEXCOORD_0");
    altered = valid;
    altered["buffers"][0]["byteLength"] = 137;
    Reject(fixture, altered, "/buffers/0/byteLength");
    altered = valid;
    altered["buffers"][0]["uri"] = "missing.bin";
    Reject(fixture, altered, "/buffers/0/uri");
    altered = valid;
    altered["buffers"][0]["uri"] = "%2e%2e/escape.bin";
    Reject(fixture, altered, "/buffers/0/uri");
    altered = valid;
    altered["buffers"][0]["uri"] = "data:application/octet-stream;base64,AA==";
    Reject(fixture, altered, "/buffers/0/uri");
    altered = valid;
    altered["images"][0]["uri"] = "%GG.png";
    Reject(fixture, altered, "/images/0/uri");
    altered = valid;
    altered["images"][0]["uri"] = "missing.png";
    Reject(fixture, altered, "/images/0/uri");
    altered = valid;
    altered["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["index"] = 9;
    Reject(fixture, altered, "/textures/9");
    altered = valid;
    altered["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = {2, 1, 1, 1};
    Reject(fixture, altered, "/baseColorFactor/0");
    altered = valid;
    altered["materials"][0]["alphaMode"] = "BLEND";
    Reject(fixture, altered, "/materials/0/alphaMode");
    altered = valid;
    altered["nodes"][0]["translation"] = {1, 0, 0};
    Reject(fixture, altered, "/nodes/0/translation");
    altered = valid;
    altered["extensionsUsed"] = {"KHR_materials_unlit"};
    Reject(fixture, altered, "/extensionsUsed");
    altered = valid;
    altered["nodes"].push_back({{"mesh", 0}});
    Reject(fixture, altered, "/nodes");
    altered = valid;
    altered["accessors"] = Json::object();
    Reject(fixture, altered, "/accessors");
    const uint32_t badIndex = 99;
    std::memcpy(binary.data() + 124, &badIndex, 4);
    WriteBytes(binaryPath, binary);
    Reject(fixture, valid, "/accessors/3");
    std::memcpy(binary.data() + 124, indexData, 12);
    const float invalid = std::nanf("");
    std::memcpy(binary.data() + 20, &invalid, 4);
    WriteBytes(binaryPath, binary);
    Reject(fixture, valid, "/accessors/0");
    {
        std::ofstream stream(fixture);
        stream << "{invalid json";
    }
    try
    {
        LoadGltfAlbedo(fixture);
        throw std::runtime_error("Invalid JSON accepted");
    }
    catch (const std::exception &e)
    {
        Require(std::string(e.what()).find("Invalid JSON near byte") != std::string::npos,
                "JSON syntax diagnostic");
    }
    std::cout << "glTF offsets/stride/16-32-bit indices/coordinates/UV/material/URI/failure tests passed\n";
}
