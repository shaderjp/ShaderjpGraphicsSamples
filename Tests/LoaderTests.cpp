#include "Application.h"
#include "Image.h"
#include "Mesh.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <objbase.h>
#include <stdexcept>
using namespace tutorial;
void TestGltfLoaders(const std::filesystem::path &root, const std::filesystem::path &directory);
namespace
{
void Require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
std::filesystem::path directory;
Mesh Parse(const std::string &text)
{
    const auto path = directory / "fixture.obj";
    {
        std::ofstream output(path);
        output << text;
    }
    return LoadObj(path);
}
void MustReject(const std::string &text, const std::string &location)
{
    try
    {
        Parse(text);
    }
    catch (const std::exception &error)
    {
        Require(std::string(error.what()).find("fixture.obj:" + location + ":") != std::string::npos,
                "OBJ diagnostic must include the offending line");
        return;
    }
    throw std::runtime_error("Invalid OBJ unexpectedly accepted");
}
} // namespace
int main(int argc, char **argv)
{
    try
    {
        Require(argc == 2, "Pass the repository directory");
        const std::filesystem::path root(argv[1]);
        directory = root / ".cache/loader-tests";
        std::filesystem::create_directories(directory);
        const std::string triangle = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\n";
        auto mesh = Parse(triangle + "f -3/-3 -2/-2 -1/-1 # negative indices\n");
        Require(mesh.vertices.size() == 3 && mesh.indices.size() == 3, "Negative indices");
        Require(mesh.vertices[0].uv.y == 1 && mesh.vertices[2].uv.y == 0, "OBJ V direction");
        mesh = Parse(triangle + "vt 0.5 0.5\nf 1/1 2/2 3/3\nf 1/4 3/3 2/2\n");
        Require(mesh.vertices.size() == 4 && mesh.indices.size() == 6, "UV seams must split vertices");
        mesh = Parse(triangle + "vn 0 0 1\nvn 0 0 -1\nf 1/1/1 2/2/1 3/3/1\nf 1/1/2 3/3/2 2/2/2\n");
        Require(mesh.vertices.size() == 6, "Normal seams must split vertices");
        mesh = Parse("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0.5 1.5 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0.5 1\nvt "
                     "0 1\nf 1/1 2/2 3/3 4/4 5/5\n");
        Require(mesh.vertices.size() == 5 && mesh.indices.size() == 9, "Convex polygon triangulation");
        MustReject(triangle + "f 0/1 2/2 3/3\n", "7");
        MustReject(triangle + "f 4/1 2/2 3/3\n", "7");
        MustReject(triangle + "f -4/1 2/2 3/3\n", "7");
        MustReject(triangle + "f 1//1 2//1 3//1\n", "7");
        MustReject(triangle + "f 1/1/1 2/2/1 3/3/1\n", "7");
        MustReject(triangle + "f 1/1 2/2\n", "7");
        MustReject("v nan 0 0\n", "1");
        MustReject(triangle, "6");
        mesh = LoadObj(root / "Assets/Models/Cube.obj");
        Require(mesh.vertices.size() == 24 && mesh.indices.size() == 36, "Bundled cube");
        TestGltfLoaders(root, directory);
        CheckHR(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Test COM init");
        const auto image = LoadWicImage(root / "Assets/Textures/UV.png");
        Require(image.width == 256 && image.height == 256 && image.rgba[0] == 218 && image.rgba[2] == 62,
                "WIC must return RGBA");
        // Distinct red/blue pixels and padded rows catch encoder channel/stride errors.
        const uint8_t pixels[] = {255, 0,   0, 255, 0,   0,   255, 255, 0, 0, 0, 0,
                                  0,   255, 0, 255, 255, 255, 0,   255, 0, 0, 0, 0};
        SavePng(directory / "roundtrip.png", 2, 2, pixels, 12);
        const auto roundtrip = LoadWicImage(directory / "roundtrip.png");
        Require(roundtrip.rgba ==
                    std::vector<uint8_t>({255, 0, 0, 255, 0, 0, 255, 255, 0, 255, 0, 255, 255, 255, 0, 255}),
                "PNG RGBA roundtrip");
        bool missingImage = false;
        try
        {
            LoadWicImage(directory / "missing.png");
        }
        catch (const std::exception &error)
        {
            missingImage = std::string(error.what()).find("missing.png") != std::string::npos;
        }
        Require(missingImage, "Missing texture must report its path");
        bool missingBinary = false;
        try
        {
            ReadBinary(directory / "missing.cso");
        }
        catch (const std::exception &error)
        {
            missingBinary = std::string(error.what()).find("missing.cso") != std::string::npos;
        }
        Require(missingBinary, "Missing shader must report its path");
        CoUninitialize();
        std::cout << "OBJ/glTF/WIC/binary loader tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
