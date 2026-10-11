#include "Gltf.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

namespace tutorial
{
namespace
{
using Json = nlohmann::json;
class Reader
{
  public:
    explicit Reader(const std::filesystem::path &path) : path_(std::filesystem::absolute(path))
    {
        const auto bytes = ReadFile(path_, "/", 16 * 1024 * 1024);
        try
        {
            document_ = Json::parse(bytes);
        }
        catch (const Json::parse_error &e)
        {
            Fail("/", "Invalid JSON near byte " + std::to_string(e.byte) + ": " + e.what());
        }
        Check(document_.is_object(), "/", "Expected a JSON object");
        RejectExtensions(document_, "");
    }
    [[noreturn]] void Fail(const std::string &at, const std::string &message) const
    {
        throw std::runtime_error(path_.string() + ": " + at + ": " + message);
    }
    void Check(bool condition, const std::string &at, const std::string &message) const
    {
        if (!condition)
            Fail(at, message);
    }
    const Json &Get(const std::string &at) const
    {
        try
        {
            return document_.at(Json::json_pointer(at));
        }
        catch (const Json::exception &)
        {
            Fail(at, "Missing or invalid JSON member/reference");
        }
    }
    size_t Unsigned(const Json &value, const std::string &at) const
    {
        Check(value.is_number_unsigned() || (value.is_number_integer() && value.get<int64_t>() >= 0), at,
              "Expected a nonnegative integer");
        const uint64_t n = value.get<uint64_t>();
        Check(n <= std::numeric_limits<size_t>::max(), at, "Integer exceeds address range");
        return static_cast<size_t>(n);
    }
    size_t Number(const std::string &at) const
    {
        return Unsigned(Get(at), at);
    }
    size_t NumberOr(const Json &object, const char *key, const std::string &at, size_t fallback) const
    {
        return object.contains(key) ? Unsigned(object.at(key), at + "/" + key) : fallback;
    }
    std::string Text(const Json &value, const std::string &at) const
    {
        Check(value.is_string(), at, "Expected a string");
        return value.get<std::string>();
    }
    void Single(const char *key) const
    {
        const std::string at = std::string("/") + key;
        const auto &value = Get(at);
        Check(value.is_array() && value.size() == 1, at,
              "This sample supports exactly one " + std::string(key));
    }
    void Empty(const Json &object, const char *key, const std::string &at) const
    {
        Check(!object.contains(key) || (object[key].is_array() && object[key].empty()), at + "/" + key,
              "Unsupported feature");
    }
    bool BooleanOr(const Json &object, const char *key, const std::string &at, bool fallback) const
    {
        if (!object.contains(key))
            return fallback;
        Check(object[key].is_boolean(), at + "/" + key, "Expected a boolean");
        return object[key].get<bool>();
    }
    std::filesystem::path External(const Json &value, const std::string &at) const
    {
        const auto uri = Text(value, at);
        auto hex = [](char c) -> int
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        std::string decoded;
        for (size_t i = 0; i < uri.size(); ++i)
        {
            char c = uri[i];
            if (c == '%')
            {
                Check(i + 2 < uri.size() && hex(uri[i + 1]) >= 0 && hex(uri[i + 2]) >= 0, at,
                      "Invalid URI escape");
                c = static_cast<char>(hex(uri[i + 1]) * 16 + hex(uri[i + 2]));
                i += 2;
            }
            Check(c != '\0' && c != '\\' && c != ':' && c != '?' && c != '#', at,
                  "Only relative file URIs are supported");
            decoded += c;
        }
        const std::filesystem::path relative(std::u8string(decoded.begin(), decoded.end()));
        Check(!relative.empty() && !relative.has_root_path(), at, "Expected a relative external file");
        const auto root = std::filesystem::weakly_canonical(path_.parent_path());
        const auto resolved = std::filesystem::weakly_canonical(root / relative);
        const auto within = resolved.lexically_relative(root);
        Check(!within.empty() && *within.begin() != "..", at, "External URI escapes the model directory");
        Check(std::filesystem::is_regular_file(resolved), at, "Missing external file: " + resolved.string());
        return resolved;
    }
    std::vector<uint8_t> ReadFile(const std::filesystem::path &file, const std::string &at,
                                  size_t limit) const
    {
        Check(std::filesystem::is_regular_file(file), at, "Missing file: " + file.string());
        Check(std::filesystem::file_size(file) <= limit, at,
              "File exceeds this tutorial's size limit: " + file.string());
        return ReadBinary(file);
    }
    void ReadBuffers()
    {
        const auto &entries = Get("/buffers");
        Check(entries.is_array() && !entries.empty(), "/buffers", "Expected external buffers");
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const std::string at = "/buffers/" + std::to_string(i);
            const auto length = Number(at + "/byteLength");
            auto bytes = ReadFile(External(Get(at + "/uri"), at + "/uri"), at, 256 * 1024 * 1024);
            Check(length > 0 && length <= bytes.size(), at + "/byteLength", "External buffer is truncated");
            bytes.resize(length);
            buffers_.push_back(std::move(bytes));
        }
    }
    struct Accessor
    {
        const uint8_t *bytes = nullptr;
        size_t count = 0, stride = 0, width = 0, componentBytes = 0;
        uint32_t component = 0;
        std::string at;
    };
    Accessor ReadAccessor(const Json &index, const std::string &reference, const char *type,
                          bool indices) const
    {
        const std::string at = "/accessors/" + std::to_string(Unsigned(index, reference));
        const auto &entry = Get(at);
        Check(entry.is_object(), at, "Expected an accessor object");
        Check(!entry.contains("sparse") && !BooleanOr(entry, "normalized", at, false), at,
              "Sparse/normalized accessors are unsupported");
        Check(Text(Get(at + "/type"), at + "/type") == type, at + "/type", "Unexpected accessor type");
        const size_t component = Number(at + "/componentType");
        Check(indices ? (component == 5123 || component == 5125) : component == 5126, at + "/componentType",
              indices ? "Expected 16/32-bit unsigned indices" : "Expected FLOAT vertex data");
        const size_t width = std::string(type) == "VEC3"   ? 3
                             : std::string(type) == "VEC2" ? 2
                             : std::string(type) == "VEC4" ? 4
                                                           : 1;
        const size_t componentBytes = component == 5123 ? 2 : 4;
        const size_t count = Number(at + "/count");
        Check(count > 0 && count <= UINT32_MAX / (indices ? sizeof(uint32_t) : sizeof(ModelVertex)),
              at + "/count", "Invalid/too large element count");
        const std::string viewAt = "/bufferViews/" + std::to_string(Number(at + "/bufferView"));
        const auto &view = Get(viewAt);
        Check(view.is_object(), viewAt, "Expected a buffer view object");
        const auto bufferIndex = Number(viewAt + "/buffer");
        Check(bufferIndex < buffers_.size(), viewAt + "/buffer", "Buffer index out of range");
        const auto &buffer = buffers_[bufferIndex];
        const size_t viewOffset = NumberOr(view, "byteOffset", viewAt, 0);
        const size_t length = Number(viewAt + "/byteLength");
        Check(viewOffset <= buffer.size() && length <= buffer.size() - viewOffset, viewAt,
              "Buffer view exceeds buffer");
        const size_t offset = NumberOr(entry, "byteOffset", at, 0);
        const size_t elementBytes = width * componentBytes;
        const size_t stride = NumberOr(view, "byteStride", viewAt, elementBytes);
        Check(!view.contains("byteStride") || (!indices && stride >= 4 && stride <= 252 && stride % 4 == 0),
              viewAt + "/byteStride", "Invalid byteStride");
        Check(stride >= elementBytes && viewOffset % componentBytes == 0 && offset % componentBytes == 0,
              at + "/byteOffset", "Invalid stride/alignment");
        if (view.contains("target"))
            Check(Number(viewAt + "/target") == (indices ? 34963u : 34962u), viewAt + "/target",
                  "Unexpected buffer view target");
        // Subtract before dividing: no overflow-prone offset + count * stride.
        Check(offset <= length && elementBytes <= length - offset &&
                  count - 1 <= (length - offset - elementBytes) / stride,
              at, "Accessor exceeds buffer view");
        return {buffer.data() + viewOffset + offset, count, stride, width, componentBytes,
                static_cast<uint32_t>(component),    at};
    }
    float Float(const Accessor &entry, size_t vertex, size_t channel) const
    {
        static_assert(std::endian::native == std::endian::little);
        float value;
        std::memcpy(&value, entry.bytes + vertex * entry.stride + channel * 4, sizeof(value));
        Check(std::isfinite(value), entry.at, "Nonfinite vertex attribute");
        return value;
    }
    uint32_t Index(const Accessor &entry, size_t i) const
    {
        const auto *bytes = entry.bytes + i * entry.stride;
        if (entry.componentBytes == 2)
        {
            uint16_t value;
            std::memcpy(&value, bytes, 2);
            return value;
        }
        uint32_t value;
        std::memcpy(&value, bytes, 4);
        return value;
    }
    float Factor(const Json &value, const std::string &at) const
    {
        Check(value.is_number(), at, "Expected a numeric factor");
        const float f = value.get<float>();
        Check(std::isfinite(f) && f >= 0 && f <= 1, at, "Factor must be in [0,1]");
        return f;
    }

    std::pair<std::filesystem::path, GltfSampler> Texture(const std::string &infoAt) const
    {
        std::pair<std::filesystem::path, GltfSampler> result;
        const auto &textureInfo = Get(infoAt);
        Check(textureInfo.is_object(), infoAt, "Expected texture info object");
        Check(NumberOr(textureInfo, "texCoord", infoAt, 0) == 0, infoAt + "/texCoord",
              "Only UV0 is supported");
        const std::string textureAt = "/textures/" + std::to_string(Number(infoAt + "/index"));
        const auto &texture = Get(textureAt);
        const std::string imageAt = "/images/" + std::to_string(Number(textureAt + "/source"));
        const auto &image = Get(imageAt);
        Check(!image.contains("bufferView"), imageAt + "/bufferView",
              "Only external PNG images are supported");
        result.first = External(Get(imageAt + "/uri"), imageAt + "/uri");
        Check(result.first.extension() == ".png", imageAt + "/uri", "Only PNG is supported");
        if (image.contains("mimeType"))
            Check(Text(image["mimeType"], imageAt + "/mimeType") == "image/png", imageAt + "/mimeType",
                  "Expected image/png");
        if (texture.contains("sampler"))
        {
            const std::string at = "/samplers/" + std::to_string(Number(textureAt + "/sampler"));
            const auto &sampler = Get(at);
            auto read = [&](const char *key, size_t fallback, std::initializer_list<uint32_t> valid)
            {
                const auto value = NumberOr(sampler, key, at, fallback);
                Check(std::find(valid.begin(), valid.end(), value) != valid.end(), at + "/" + key,
                      "Unsupported sampler enum");
                return static_cast<uint32_t>(value);
            };
            result.second.magFilter = read("magFilter", 9729, {9728, 9729});
            result.second.minFilter = read("minFilter", 9987, {9728, 9729, 9984, 9985, 9986, 9987});
            result.second.wrapS = read("wrapS", 10497, {33071, 33648, 10497});
            result.second.wrapT = read("wrapT", 10497, {33071, 33648, 10497});
        }
        return result;
    }
    float Scalar(const Json &value, const std::string &at) const
    {
        Check(value.is_number(), at, "Expected a number");
        const float number = value.get<float>();
        Check(std::isfinite(number), at, "Expected a finite number");
        return number;
    }

  private:
    void RejectExtensions(const Json &value, const std::string &at) const
    {
        if (value.is_object())
            for (const auto &[key, child] : value.items())
            {
                const std::string childAt = at + "/" + key;
                if (key == "extensions" || key == "extensionsUsed" || key == "extensionsRequired")
                    Check(child.empty(), childAt, "Extensions are unsupported in this sample");
                if (key != "extras")
                    RejectExtensions(child, childAt);
            }
        else if (value.is_array())
            for (size_t i = 0; i < value.size(); ++i)
                RejectExtensions(value[i], at + "/" + std::to_string(i));
    }
    std::filesystem::path path_;
    Json document_;
    std::vector<std::vector<uint8_t>> buffers_;
};
} // namespace

static GltfModel LoadGltf(const std::filesystem::path &path, bool pbrInputs)
{
    Reader r(path);
    const auto &root = r.Get("");
    for (const auto key : {"accessors", "bufferViews", "textures", "images"})
        r.Check(r.Get(std::string("/") + key).is_array(), std::string("/") + key, "Expected a JSON array");
    r.Check(r.Text(r.Get("/asset/version"), "/asset/version") == "2.0", "/asset/version",
            "Only glTF 2.0 is supported");
    if (r.Get("/asset").contains("minVersion"))
        r.Check(r.Text(r.Get("/asset/minVersion"), "/asset/minVersion") == "2.0", "/asset/minVersion",
                "Unsupported minimum version");
    for (const auto key : {"animations", "skins", "cameras"})
        r.Empty(root, key, "");
    for (const auto key : {"scenes", "nodes", "meshes", "materials"})
        r.Single(key);
    r.Check(r.NumberOr(root, "scene", "", 0) == 0, "/scene", "Scene index out of range");
    r.Check(r.Get("/scenes/0/nodes") == Json::array({0}), "/scenes/0/nodes", "Scene must select node 0");
    const auto &node = r.Get("/nodes/0");
    r.Check(r.Number("/nodes/0/mesh") == 0, "/nodes/0/mesh", "Mesh index out of range");
    r.Empty(node, "children", "/nodes/0");
    for (const auto key : {"matrix", "translation", "rotation", "scale", "skin", "camera", "weights"})
        r.Check(!node.contains(key), std::string("/nodes/0/") + key,
                "Apply transforms; this sample supports an identity static node");
    const auto &primitives = r.Get("/meshes/0/primitives");
    r.Check(primitives.is_array() && primitives.size() == 1, "/meshes/0/primitives",
            "Expected one primitive");
    const std::string primitiveAt = "/meshes/0/primitives/0";
    const auto &primitive = r.Get(primitiveAt);
    r.Empty(primitive, "targets", primitiveAt);
    r.Empty(r.Get("/meshes/0"), "weights", "/meshes/0");
    r.Check(r.NumberOr(primitive, "mode", primitiveAt, 4) == 4, primitiveAt + "/mode",
            "Only TRIANGLES are supported");
    r.Check(r.Number(primitiveAt + "/material") == 0, primitiveAt + "/material",
            "Material index out of range");
    const auto &attributes = r.Get(primitiveAt + "/attributes");
    r.Check(attributes.is_object(), primitiveAt + "/attributes", "Expected attributes object");
    for (const auto &[key, value] : attributes.items())
        r.Check(key == "POSITION" || key == "NORMAL" || key == "TEXCOORD_0" || key == "TANGENT",
                primitiveAt + "/attributes/" + key, "Unsupported vertex attribute");
    r.ReadBuffers();
    auto attribute = [&](const char *name, const char *type)
    {
        const auto at = primitiveAt + "/attributes/" + name;
        return r.ReadAccessor(r.Get(at), at, type, false);
    };
    const auto positions = attribute("POSITION", "VEC3"), normals = attribute("NORMAL", "VEC3"),
               uvs = attribute("TEXCOORD_0", "VEC2");
    r.Check(positions.count == normals.count && positions.count == uvs.count, primitiveAt + "/attributes",
            "Vertex attribute count mismatch");
    r.Check(!pbrInputs || positions.count <= UINT32_MAX / 48, positions.at + "/count",
            "PBR vertex buffer exceeds the 32-bit view size");
    GltfModel result;
    const auto limit = std::numeric_limits<float>::max();
    result.minimum = {limit, limit, limit};
    result.maximum = {-limit, -limit, -limit};
    result.mesh.vertices.reserve(positions.count);
    for (size_t i = 0; i < positions.count; ++i)
    {
        // Reflect Z and preserve UV. Unlike OBJ, glTF has no 1-v conversion.
        ModelVertex vertex{{r.Float(positions, i, 0), r.Float(positions, i, 1), -r.Float(positions, i, 2)},
                           {r.Float(uvs, i, 0), r.Float(uvs, i, 1)},
                           {r.Float(normals, i, 0), r.Float(normals, i, 1), -r.Float(normals, i, 2)}};
        const auto n = DirectX::XMLoadFloat3(&vertex.normal);
        r.Check(std::abs(DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(n)) - 1) < 1e-3f, normals.at,
                "Normal must be unit length");
        result.minimum = {std::min(result.minimum.x, vertex.position.x),
                          std::min(result.minimum.y, vertex.position.y),
                          std::min(result.minimum.z, vertex.position.z)};
        result.maximum = {std::max(result.maximum.x, vertex.position.x),
                          std::max(result.maximum.y, vertex.position.y),
                          std::max(result.maximum.z, vertex.position.z)};
        result.mesh.vertices.push_back(vertex);
    }
    if (attributes.contains("TANGENT"))
    {
        const auto tangents = attribute("TANGENT", "VEC4");
        r.Check(tangents.count == positions.count, tangents.at, "Tangent count mismatch");
        for (size_t i = 0; i < tangents.count; ++i)
        {
            DirectX::XMFLOAT4 t{r.Float(tangents, i, 0), r.Float(tangents, i, 1), -r.Float(tangents, i, 2),
                                -r.Float(tangents, i, 3)};
            r.Check(t.w == 1 || t.w == -1, tangents.at, "Tangent W must be +/-1");
            r.Check(std::abs(t.x * t.x + t.y * t.y + t.z * t.z - 1) < 1e-3f, tangents.at,
                    "Tangent must be unit length");
            const auto &n = result.mesh.vertices[i].normal;
            r.Check(std::abs(n.x * t.x + n.y * t.y + n.z * t.z) < 1e-3f, tangents.at,
                    "Tangent must be perpendicular to normal");
            result.tangents.push_back(t);
        }
    }
    const auto indices =
        r.ReadAccessor(r.Get(primitiveAt + "/indices"), primitiveAt + "/indices", "SCALAR", true);
    r.Check(indices.count % 3 == 0, indices.at + "/count", "Triangle index count must be divisible by three");
    result.mesh.indices.reserve(indices.count);
    for (size_t i = 0; i < indices.count; i += 3)
    {
        const std::array<uint32_t, 3> triangle{r.Index(indices, i), r.Index(indices, i + 2),
                                               r.Index(indices, i + 1)};
        for (const auto index : triangle)
            r.Check(index < positions.count, indices.at, "Index exceeds vertex count");
        result.mesh.indices.insert(result.mesh.indices.end(), triangle.begin(), triangle.end());
    }
    const auto &material = r.Get("/materials/0");
    r.Check(material.is_object(), "/materials/0", "Expected a material object");
    r.Check(!material.contains("alphaMode") ||
                r.Text(material["alphaMode"], "/materials/0/alphaMode") == "OPAQUE",
            "/materials/0/alphaMode", "Only OPAQUE materials are supported");
    r.Check(!r.BooleanOr(material, "doubleSided", "/materials/0", false), "/materials/0/doubleSided",
            "Only single-sided materials are supported");
    const std::string pbrAt = "/materials/0/pbrMetallicRoughness";
    const auto &pbr = r.Get(pbrAt);
    if (pbr.contains("baseColorFactor"))
    {
        const auto &factor = r.Get(pbrAt + "/baseColorFactor");
        r.Check(factor.is_array() && factor.size() == 4, pbrAt + "/baseColorFactor", "Expected four factors");
        result.baseColorFactor = {r.Factor(factor[0], pbrAt + "/baseColorFactor/0"),
                                  r.Factor(factor[1], pbrAt + "/baseColorFactor/1"),
                                  r.Factor(factor[2], pbrAt + "/baseColorFactor/2"),
                                  r.Factor(factor[3], pbrAt + "/baseColorFactor/3")};
    }
    const auto albedo = r.Texture(pbrAt + "/baseColorTexture");
    result.albedoPath = albedo.first;
    result.sampler = albedo.second;
    if (pbrInputs)
    {
        r.Check(result.tangents.size() == result.mesh.vertices.size(), primitiveAt + "/attributes/TANGENT",
                "Sample 08 requires explicit tangents");
        for (size_t i = 0; i < result.mesh.indices.size(); i += 3)
            r.Check(result.tangents[result.mesh.indices[i]].w ==
                            result.tangents[result.mesh.indices[i + 1]].w &&
                        result.tangents[result.mesh.indices[i]].w ==
                            result.tangents[result.mesh.indices[i + 2]].w,
                    primitiveAt + "/attributes/TANGENT", "Triangle tangent signs must agree");
        auto texture = [&](const std::string &at)
        {
            const auto loaded = r.Texture(at);
            r.Check(loaded.second == result.sampler, at,
                    "Sample 08 requires one shared sampler for all material textures");
            return loaded.first;
        };
        result.normalPath = texture("/materials/0/normalTexture");
        result.ormPath = texture(pbrAt + "/metallicRoughnessTexture");
        const auto occlusion = texture("/materials/0/occlusionTexture");
        r.Check(occlusion == result.ormPath, "/materials/0/occlusionTexture",
                "Sample 08 requires packed ORM: occlusion and metallicRoughness use the same PNG");
        result.emissivePath = texture("/materials/0/emissiveTexture");
        if (pbr.contains("metallicFactor"))
            result.metallicFactor = r.Factor(pbr["metallicFactor"], pbrAt + "/metallicFactor");
        if (pbr.contains("roughnessFactor"))
            result.roughnessFactor = r.Factor(pbr["roughnessFactor"], pbrAt + "/roughnessFactor");
        const auto &normal = r.Get("/materials/0/normalTexture");
        if (normal.contains("scale"))
            result.normalScale = r.Scalar(normal["scale"], "/materials/0/normalTexture/scale");
        const auto &ao = r.Get("/materials/0/occlusionTexture");
        if (ao.contains("strength"))
            result.occlusionStrength = r.Factor(ao["strength"], "/materials/0/occlusionTexture/strength");
        if (material.contains("emissiveFactor"))
        {
            const auto &factor = material["emissiveFactor"];
            r.Check(factor.is_array() && factor.size() == 3, "/materials/0/emissiveFactor",
                    "Expected three emissive factors");
            result.emissiveFactor = {r.Factor(factor[0], "/materials/0/emissiveFactor/0"),
                                     r.Factor(factor[1], "/materials/0/emissiveFactor/1"),
                                     r.Factor(factor[2], "/materials/0/emissiveFactor/2")};
        }
    }
    return result;
}

GltfModel LoadGltfAlbedo(const std::filesystem::path &path)
{
    return LoadGltf(path, false);
}
GltfModel LoadGltfPbr(const std::filesystem::path &path)
{
    return LoadGltf(path, true);
}

GltfSceneConstants MakeGltfSceneConstants(uint32_t width, uint32_t height, const GltfModel &model)
{
    using namespace DirectX;
    const auto minimum = XMLoadFloat3(&model.minimum), maximum = XMLoadFloat3(&model.maximum);
    const auto center = (minimum + maximum) * 0.5f;
    const auto extent = maximum - minimum;
    const float radius =
        std::max({XMVectorGetX(extent), XMVectorGetY(extent), XMVectorGetZ(extent), 0.002f}) * 0.5f;
    const float aspect = float(width) / float(height);
    const float distance = radius * 3.8f / std::min(1.0f, aspect);
    const auto direction = XMVector3Normalize(XMVectorSet(-0.82f, 0.32f, 0.56f, 0));
    const auto view = XMMatrixLookAtLH(center + direction * distance, center, XMVectorSet(0, 1, 0, 0));
    const auto projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, std::max(0.001f, distance * 0.01f),
                                                     distance + radius * 10);
    GltfSceneConstants result;
    XMStoreFloat4x4(&result.mvp, view * projection);
    result.baseColorFactor = model.baseColorFactor;
    return result;
}
} // namespace tutorial
