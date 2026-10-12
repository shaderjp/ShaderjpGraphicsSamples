#include "Workshop.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <objbase.h>
#include <stdexcept>
using namespace tutorial;
void Require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char **argv)
{
    try
    {
        CheckHR(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM initialization");
        if (argc != 2)
            throw std::runtime_error("Expected repository root");
        auto w = LoadWorkshop(std::filesystem::path(argv[1]) / "Assets/Scenes/Workshop/Workshop.json");
        Require(w.primitives.size() == 62 && w.materials.size() == 16 && w.images.size() == 40,
                "unique counts");
        Require(w.scenes.size() == 2 && w.path.size() == 1201, "scene/path count");
        for (auto &s : w.scenes)
        {
            uint64_t triangles = 0;
            for (auto &i : s.instances)
                triangles += w.primitives[i.primitive].indices.size() / 3;
            bool standard = s.name == "Standard";
            Require(s.instances.size() == (standard ? 201u : 1809u), "instance count");
            Require(triangles == (standard ? 251200u : 2260800u) && s.lights.size() == (standard ? 8u : 72u),
                    "triangle/light count");
            std::cout << s.name << ": " << s.instances.size() << " primitives, " << triangles
                      << " triangles, " << s.lights.size() << " point lights\n";
        }
        double worst = 0;
        for(const auto &axis:std::array<DirectX::XMFLOAT3,8>{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{.70710678f,0,-.70710678f},{0,.70710678f,-.70710678f}}}) {
            auto e=EncodeOctahedral(axis);auto d=DecodeOctahedral(e);
            Require(std::abs(d.x-axis.x)<1e-6f&&std::abs(d.y-axis.y)<1e-6f&&std::abs(d.z-axis.z)<1e-6f,"oct poles/fold seam");
        }
        for (int i = 0; i < 100000; ++i)
        {
            double z = 1 - 2 * (i + .5) / 100000, phi = i * 2.399963229728653;
            float a = float(std::sqrt(1 - z * z));
            DirectX::XMFLOAT3 n{a * float(std::cos(phi)), a * float(std::sin(phi)), float(z)};
            auto e = EncodeOctahedral(n);
            e.x = std::round(e.x * 32767) / 32767;
            e.y = std::round(e.y * 32767) / 32767;
            auto d = DecodeOctahedral(e);
            double dx = n.x - d.x, dy = n.y - d.y, dz = n.z - d.z;
            double angle =
                2 * std::asin(std::sqrt(dx * dx + dy * dy + dz * dz) / 2) * 180 / 3.141592653589793;
            worst = std::max(worst, angle);
        }
        Require(worst < .01, "oct angular error");
        std::cout << "Oct max degrees: " << worst << "\n";
        Image im{3, 3, std::vector<uint8_t>(36, 255)};
        auto m = GenerateMipChain(im, ImageSemantic::Color);
        Require(m.size() == 2 && m.back().rgba[0] == 255, "odd mip");
        im = {2, 1, {0, 0, 0, 255, 255, 255, 255, 255}};
        m = GenerateMipChain(im, ImageSemantic::Color);
        Require(m.back().rgba[0] >= 187 && m.back().rgba[0] <= 189, "linear color averaging");
        im = {2, 1, {128, 128, 255, 255, 128, 128, 255, 255}};
        m = GenerateMipChain(im, ImageSemantic::Normal);
        Require(m.back().rgba[2] == 255, "normal renormalization");
        for (auto &image : w.images)
        {
            auto chain = GenerateMipChain(LoadWicImage(image.path), image.semantic, image.alphaCutoff);
            Require(chain.back().width == 1 && chain.back().height == 1, "mip termination");
        }
        using Json = nlohmann::json;
        const auto repo = std::filesystem::path(argv[1]),
                   model = repo / "Assets/Models/Workshop/Workshop.gltf";
        auto gltf = Json::parse(ReadBinary(model));
        auto config = Json::parse(ReadBinary(repo / "Assets/Scenes/Workshop/Workshop.json"));
        auto temp = repo / ".cache/workshop-loader/negative";
        std::filesystem::create_directories(temp);
        std::filesystem::copy(model.parent_path() / "Textures", temp / "Textures",
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(repo / "Assets/Scenes/Workshop/CameraPath.json", temp / "CameraPath.json",
                                   std::filesystem::copy_options::overwrite_existing);
        config["model"] = "Workshop.gltf";
        std::ofstream(temp / "Workshop.json") << config.dump();
        const auto bin = ReadBinary(model.parent_path() / "Workshop.bin");
        for (int test = 0; test < 6; ++test)
        {
            auto j = gltf;
            auto b = bin;
            if (test == 0)
                j["nodes"][0]["children"] = Json::array({0});
            if (test == 1)
                j["materials"][1]["extras"]["id"] = j["materials"][0]["extras"]["id"];
            if (test == 2)
                j["extensionsRequired"] = Json::array({"KHR_draco_mesh_compression"});
            if (test == 3)
                j["accessors"][0]["count"] = UINT64_MAX;
            if (test == 4)
            {
                auto &a = j["accessors"][0];
                auto &v = j["bufferViews"][a["bufferView"].get<size_t>()];
                size_t offset = v.value("byteOffset", size_t(0)) + a.value("byteOffset", size_t(0));
                float value = std::numeric_limits<float>::quiet_NaN();
                std::memcpy(b.data() + offset, &value, 4);
            }
            if (test == 5)
            {
                auto ai = j["meshes"][0]["primitives"][0]["indices"].get<size_t>();
                auto &a = j["accessors"][ai];
                auto &v = j["bufferViews"][a["bufferView"].get<size_t>()];
                size_t offset = v.value("byteOffset", size_t(0)) + a.value("byteOffset", size_t(0));
                std::memset(b.data() + offset, 255, a["componentType"] == 5123 ? 2 : 4);
            }
            std::ofstream(temp / "Workshop.gltf") << j.dump();
            std::ofstream out(temp / "Workshop.bin", std::ios::binary);
            out.write(reinterpret_cast<const char *>(b.data()), b.size());
            out.close();
            bool rejected = false;
            try
            {
                LoadWorkshop(temp / "Workshop.json");
            }
            catch (const std::exception &e)
            {
                rejected = true;
                std::cout << "Rejected fixture " << test << ": " << e.what() << '\n';
            }
            Require(rejected, "negative fixture accepted");
        }
        std::cout << "Workshop CPU tests passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
