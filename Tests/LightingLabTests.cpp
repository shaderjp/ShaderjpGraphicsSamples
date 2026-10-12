#include "DdsImage.h"
#include "LightingScene.h"
#include <DirectXPackedVector.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
using namespace tutorial;
void Require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
int main(int argc, char **argv)
{
    try
    {
        Require(argc == 2, "Repository root argument required");
        const std::filesystem::path root = argv[1];
        const auto env = root / "Assets/Environments/LightingLab";
        auto image = LoadDds(env / "Studio_Environment.dds");
        Require(image.cube && image.faces == 6 && image.mips == 1 && image.width == 512,
                "Environment cube metadata");
        auto spec = LoadDds(env / "Room_Specular.dds");
        Require(spec.mips == 9 && spec.subresources.size() == 54, "All cube faces/mips retained");
        Require(spec.subresources.back().width == 1 && spec.subresources.back().face == 5,
                "Face-major DDS layout");
        auto lut = LoadDds(env / "BRDF.dds");
        Require(!lut.cube && lut.format == DdsFormat::Rg16Float && lut.width == 256, "BRDF LUT format");
        auto scene = LoadLightingScene(root / "Assets/Scenes/LightingLab/Exhibition.json");
        Require(scene.objects.size() == 18, "Scene contains complete exhibition");
        Require(scene.camera.z < 0 && scene.objects.back().model.mesh.indices.size() == 25920,
                "RH to LH scene and existing ShaderBall");
        auto c = MakeLabConstants(1280, 720, scene, 3, 13, 0, 1, 0, 3, 1, true, true, .001f, 0);
        auto moved = MakeLabConstants(1280, 720, scene, 3, 13, 0, 1, 0, 3, 1, true, true, .001f, 1);
        Require(std::abs(moved.lighting.world._41 - c.lighting.world._41 - 1) < 1e-5f,
                "Movable occluder world transform");
        auto work = root / ".cache/lightinglab-cpu";
        std::filesystem::create_directories(work);
        // Mask ONLY the old/new occluder silhouettes when comparing GPU frames.
        // Changes outside these bounds must come from its projected shadow.
        auto boxes = nlohmann::json::array();
        for (const auto &constants : {c, moved})
        {
            using namespace DirectX;
            const auto matrix = XMLoadFloat4x4(&constants.lighting.world) *
                                XMLoadFloat4x4(&constants.lighting.viewProjection);
            float left = 1280, top = 720, right = 0, bottom = 0;
            for (const auto &vertex : scene.objects[3].model.mesh.vertices)
            {
                const auto p = XMVector3TransformCoord(XMLoadFloat3(&vertex.position), matrix);
                const float x = (XMVectorGetX(p) + 1) * 640, y = (1 - XMVectorGetY(p)) * 360;
                left = std::min(left, x); top = std::min(top, y);
                right = std::max(right, x); bottom = std::max(bottom, y);
            }
            boxes.push_back({int(std::floor(left))-3,int(std::floor(top))-3,
                             int(std::ceil(right))+3,int(std::ceil(bottom))+3});
        }
        { std::ofstream bounds(work / "occluder-bounds.json"); bounds << boxes.dump(2); }
        auto invalid = ReadBinary(env / "BRDF.dds");
        invalid[148] = 0;
        invalid[149] = 0x7c;
        auto path = work / "nonfinite.dds";
        {
            std::ofstream f(path, std::ios::binary);
            f.write(reinterpret_cast<const char *>(invalid.data()), invalid.size());
        }
        bool rejected = false;
        try
        {
            LoadDds(path);
        }
        catch (const std::exception &e)
        {
            rejected = std::string(e.what()).find("non-finite") != std::string::npos;
        }
        Require(rejected, "Nonfinite DDS values rejected");
        invalid.resize(170);
        path = work / "truncated.dds";
        {
            std::ofstream f(path, std::ios::binary);
            f.write(reinterpret_cast<const char *>(invalid.data()), invalid.size());
        }
        rejected = false;
        try
        {
            LoadDds(path);
        }
        catch (...)
        {
            rejected = true;
        }
        Require(rejected, "Truncated DDS rejected");
        std::cout << "LightingLab DDS, scene, transform, malformed input CPU tests PASS\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
