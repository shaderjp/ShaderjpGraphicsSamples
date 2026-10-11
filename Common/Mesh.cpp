#include "Mesh.h"
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
namespace tutorial
{
namespace
{
int ResolveIndex(const std::string &text, size_t count)
{
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0)
        throw std::runtime_error("Invalid OBJ index");
    const int64_t resolved = value > 0 ? int64_t(value) - 1 : int64_t(count) + value;
    if (resolved < 0 || resolved >= static_cast<int64_t>(count))
        throw std::runtime_error("OBJ index out of range");
    return static_cast<int>(resolved);
}
template <size_t N> void ReadValues(std::istringstream &stream, std::array<float, N> &values)
{
    for (float &value : values)
        if (!(stream >> value) || !std::isfinite(value))
            throw std::runtime_error("Invalid OBJ numeric value");
}
} // namespace
Mesh LoadObj(const std::filesystem::path &path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Cannot open OBJ: " + path.string());
    std::vector<DirectX::XMFLOAT3> positions, normals;
    std::vector<DirectX::XMFLOAT2> uvs;
    std::map<std::array<int, 3>, uint32_t> uniqueVertices;
    Mesh mesh;
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(input, line))
    {
        ++lineNumber;
        try
        {
            line.resize(line.find('#') == std::string::npos ? line.size() : line.find('#'));
            std::istringstream stream(line);
            std::string type;
            stream >> type;
            if (type == "v" || type == "vn")
            {
                std::array<float, 3> value{};
                ReadValues(stream, value);
                (type == "v" ? positions : normals).emplace_back(value[0], value[1], value[2]);
            }
            else if (type == "vt")
            {
                std::array<float, 2> value{};
                ReadValues(stream, value);
                uvs.emplace_back(value[0], 1.0f - value[1]);
            }
            else if (type == "f")
            {
                std::vector<uint32_t> face;
                std::string token;
                while (stream >> token)
                {
                    const auto slash = token.find('/');
                    if (slash == std::string::npos)
                        throw std::runtime_error("Face is missing UV coordinates (expected v/vt or v/vt/vn)");
                    const auto second = token.find('/', slash + 1);
                    const std::string uv = token.substr(
                        slash + 1, second == std::string::npos ? std::string::npos : second - slash - 1);
                    if (uv.empty())
                        throw std::runtime_error("Face is missing UV coordinates");
                    int normal = -1;
                    if (second != std::string::npos)
                        normal = ResolveIndex(token.substr(second + 1), normals.size());
                    const std::array<int, 3> key{ResolveIndex(token.substr(0, slash), positions.size()),
                                                 ResolveIndex(uv, uvs.size()), normal};
                    auto [entry, inserted] =
                        uniqueVertices.try_emplace(key, static_cast<uint32_t>(mesh.vertices.size()));
                    if (inserted)
                        mesh.vertices.push_back({positions[key[0]], uvs[key[1]],
                                                 normal < 0 ? DirectX::XMFLOAT3(0, 0, 0) : normals[normal]});
                    face.push_back(entry->second);
                }
                if (face.size() < 3)
                    throw std::runtime_error("Face needs at least three vertices");
                // Fan triangulation is valid for convex polygons. Export concave faces triangulated.
                for (size_t i = 1; i + 1 < face.size(); ++i)
                {
                    mesh.indices.push_back(face[0]);
                    mesh.indices.push_back(face[i]);
                    mesh.indices.push_back(face[i + 1]);
                }
            }
            else if (!type.empty() && type != "o" && type != "g" && type != "s" && type != "mtllib" &&
                     type != "usemtl")
            {
                throw std::runtime_error("Unsupported OBJ record: " + type);
            }
        }
        catch (const std::exception &error)
        {
            throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) + ": " + error.what());
        }
    }
    if (input.bad())
        throw std::runtime_error("Cannot read OBJ: " + path.string());
    if (mesh.indices.empty())
        throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) + ": OBJ has no faces");
    return mesh;
}
SceneConstants MakeSceneConstants(uint32_t width, uint32_t height)
{
    using namespace DirectX;
    const auto model = XMMatrixIdentity();
    const auto view =
        XMMatrixLookAtLH(XMVectorSet(3.0f, 2.0f, -4.0f, 1), XMVectorZero(), XMVectorSet(0, 1, 0, 0));
    const auto projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, float(width) / float(height), 0.1f, 100.0f);
    SceneConstants result;
    XMStoreFloat4x4(&result.mvp, model * view * projection);
    return result;
}
} // namespace tutorial
