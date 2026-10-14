#include "SkinnedGltf.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace tutorial;
using namespace DirectX;
void Require(bool condition, const char *why)
{
    if (!condition)
        throw std::runtime_error(why);
}
int main(int argc, char **argv)
{
    try
    {
        if (argc != 2)
            throw std::runtime_error("Pass repository root");
        std::filesystem::path root = argv[1];
        auto original = LoadGltfSkinned(root / "Assets/Models/SilverRobot/SilverRobot.gltf");
        auto model = LoadGltfSkinned(root / "Assets/Models/SilverRobotAnimated/SilverRobot.gltf");
        Require(model.primitives.size() == 5 && model.skins.size() == 1 && model.skins[0].joints.size() == 53,
                "Bundled skin structure");
        Require(model.clips.size() == 8, "Six UAL clips plus A/T pose");
        auto rest = EvaluateGltfPose(model, SkinRestPose, 0);
        for (size_t p = 0; p < model.primitives.size(); ++p)
        {
            auto palette = MakeSkinPalette(model, rest, p);
            for (auto &m : palette)
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c)
                        Require(std::abs((&m._11)[r * 4 + c] - (r == c ? 1.f : 0.f)) < 3e-6f,
                                "Inverse bind identity");
            Require(model.primitives[p].vertices.size() == original.primitives[p].vertices.size(),
                    "Original geometry preserved");
        }
        for (size_t c = 0; c < model.clips.size(); ++c)
        {
            auto &clip = model.clips[c];
            Require(clip.duration > 0, "Non-empty clip");
            for (float t : {0.f, .13f, .5f, clip.duration, clip.duration + 10})
            {
                auto pose = EvaluateGltfPose(model, c, t);
                auto palette = MakeSkinPalette(model, pose, 0);
                for (auto &v : model.primitives.front().vertices)
                {
                    auto out = DeformSkinVertex(v, palette);
                    Require(std::isfinite(out.positionU.x) && std::isfinite(out.positionU.y) &&
                                std::isfinite(out.positionU.z),
                            "Finite deformation");
                }
            }
        }
        // Independently known TRS case: rotate (1,0,0) by +90 around Z and translate +2 on X.
        GltfSkinnedModel simple;
        simple.nodes.resize(2);
        simple.nodes[1].parent = 0;
        SkinChannel translation;
        translation.node = 0;
        translation.path = SkinChannelPath::Translation;
        translation.times = {0, 1};
        translation.values = {{0, 0, 0, 0}, {2, 0, 0, 0}};
        SkinChannel rotation;
        rotation.node = 1;
        rotation.path = SkinChannelPath::Rotation;
        rotation.times = {0, 1};
        rotation.values = {{0, 0, 0, 1}, {0, 0, .70710678f, .70710678f}};
        simple.clips.push_back({"Known", 1, {translation, rotation}});
        auto pose = EvaluateGltfPose(simple, 0, 1);
        auto point = XMVector3TransformCoord(XMVectorSet(1, 0, 0, 1), XMLoadFloat4x4(&pose.globals[1]));
        Require(std::abs(XMVectorGetX(point) - 2) < 1e-5f && std::abs(XMVectorGetY(point) - 1) < 1e-5f,
                "Hierarchy rotation/translation order");
        simple.clips[0].channels[0].step = true;
        pose = EvaluateGltfPose(simple, 0, .5f);
        Require(std::abs(pose.globals[0]._41) < 1e-6f, "STEP holds previous key");
        simple.clips[0].channels[1].values[1] = {0, 0, -.70710678f, -.70710678f};
        pose = EvaluateGltfPose(simple, 0, .5f);
        point = XMVector3TransformCoord(XMVectorSet(1, 0, 0, 1), XMLoadFloat4x4(&pose.globals[1]));
        Require(std::abs(XMVectorGetX(point) - .70710678f) < 1e-5f, "Quaternion shortest arc");
        auto source = root / "Assets/Models/SilverRobotAnimated";
        auto work = root / ".cache/skinning/cpu-fixtures";
        std::filesystem::create_directories(work);
        std::filesystem::copy_file(source / "SilverRobot.bin", work / "SilverRobot.bin",
                                   std::filesystem::copy_options::overwrite_existing);
        auto json = nlohmann::json::parse(ReadBinary(source / "SilverRobot.gltf"));
        // Exercise accessor representations independently of the bundled FLOAT weights / UBYTE joints.
        for (auto format : {5121, 5123})
        {
            auto fixture = json;
            auto bytes = ReadBinary(source / "SilverRobot.bin");
            auto &attributes = fixture["meshes"][0]["primitives"][0]["attributes"];
            auto append = [&](const char *name, bool weight)
            {
                size_t offset = bytes.size();
                size_t count = model.primitives[0].vertices.size();
                for (auto &v : model.primitives[0].vertices)
                    for (int k = 0; k < 4; ++k)
                    {
                        uint32_t value =
                            weight ? uint32_t(std::lround((&v.weights.x)[k] * (format == 5121 ? 255 : 65535)))
                                   : (&v.joints.x)[k];
                        bytes.push_back(uint8_t(value & 255));
                        if (format == 5123)
                            bytes.push_back(uint8_t(value >> 8));
                    }
                size_t size = bytes.size() - offset;
                while (bytes.size() % 4)
                    bytes.push_back(0);
                size_t view = fixture["bufferViews"].size();
                fixture["bufferViews"].push_back(
                    {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", size}});
                size_t accessor = fixture["accessors"].size();
                fixture["accessors"].push_back({{"bufferView", view},
                                                {"componentType", format},
                                                {"normalized", weight},
                                                {"type", "VEC4"},
                                                {"count", count}});
                attributes[name] = accessor;
            };
            append("JOINTS_0", false);
            append("WEIGHTS_0", true);
            fixture["buffers"][0]["uri"] = "packed.bin";
            fixture["buffers"][0]["byteLength"] = bytes.size();
            std::ofstream(work / "packed.bin", std::ios::binary)
                .write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
            std::ofstream(work / "packed.gltf") << fixture.dump();
            auto packed = LoadGltfSkinned(work / "packed.gltf");
            Require(packed.primitives[0].vertices.size() == model.primitives[0].vertices.size(),
                    "Packed accessor count");
            for (size_t i = 0; i < packed.primitives[0].vertices.size(); ++i)
            {
                auto &v = packed.primitives[0].vertices[i];
                float sum = v.weights.x + v.weights.y + v.weights.z + v.weights.w;
                Require(std::abs(sum - 1) < 1e-6f, "Integer weights normalized");
                Require(v.joints.x == model.primitives[0].vertices[i].joints.x, "Integer joints preserved");
            }
        }
        auto reject = [&](std::function<void(nlohmann::json &)> change)
        {
            auto bad = json;
            change(bad);
            std::ofstream(work / "bad.gltf") << bad.dump();
            bool failed = false;
            try
            {
                LoadGltfSkinned(work / "bad.gltf");
            }
            catch (const std::exception &)
            {
                failed = true;
            }
            Require(failed, "Malformed glTF must fail");
        };
        reject([](auto &d) { d["nodes"][0]["children"] = nlohmann::json::array({0}); });
        reject([](auto &d) { d["skins"][0]["joints"][0] = 999999; });
        reject([](auto &d) { d["buffers"][0]["byteLength"] = 999999999; });
        reject([](auto &d) { d["buffers"][0]["uri"] = "../SilverRobot.bin"; });
        reject([](auto &d) { d["buffers"][0]["uri"] = "missing.bin"; });
        reject([](auto &d) { d["animations"][0]["samplers"][0]["interpolation"] = "CUBICSPLINE"; });
        reject([](auto &d) { d["accessors"][0]["count"] = 999999999; });
        reject([](auto &d) { d["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_1"] = 0; });
        auto corruptBin = [&](const char *attribute, std::function<void(uint8_t *)> corrupt)
        {
            auto bytes = ReadBinary(source / "SilverRobot.bin");
            auto ai = json["meshes"][0]["primitives"][0]["attributes"][attribute].get<size_t>();
            auto &accessor = json["accessors"][ai];
            auto &view = json["bufferViews"][accessor["bufferView"].get<size_t>()];
            size_t offset = view.value("byteOffset", size_t(0)) + accessor.value("byteOffset", size_t(0));
            corrupt(bytes.data() + offset);
            std::ofstream(work / "corrupt.bin", std::ios::binary)
                .write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
            reject([](auto &d) { d["buffers"][0]["uri"] = "corrupt.bin"; });
        };
        corruptBin("WEIGHTS_0",
                   [](uint8_t *p)
                   {
                       float w = -1;
                       std::memcpy(p, &w, 4);
                   });
        corruptBin("WEIGHTS_0", [](uint8_t *p) { std::memset(p, 0, 16); });
        corruptBin("WEIGHTS_0",
                   [](uint8_t *p)
                   {
                       float w = std::numeric_limits<float>::quiet_NaN();
                       std::memcpy(p, &w, 4);
                   });
        corruptBin("JOINTS_0", [](uint8_t *p) { *p = 255; });
        auto truncated = ReadBinary(source / "SilverRobot.bin");
        std::ofstream(work / "truncated.bin", std::ios::binary)
            .write(reinterpret_cast<const char *>(truncated.data()), std::streamsize(truncated.size() - 1));
        reject([](auto &d) { d["buffers"][0]["uri"] = "truncated.bin"; });
        std::cout << "Skinning CPU: PASS (bind, clips, hierarchy, STEP, quaternion, malformed inputs)\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
