#include "Workshop.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
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
                {
                    if (child.is_object())
                        for (const auto &[extension, unused] : child.items())
                            Check(extension == "KHR_lights_punctual" ||
                                      extension == "KHR_materials_emissive_strength",
                                  childAt, "Unsupported extension: " + extension);
                    else
                        for (const auto &extension : child)
                            Check(extension == "KHR_lights_punctual" ||
                                      extension == "KHR_materials_emissive_strength",
                                  childAt, "Unsupported extension");
                }
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

static Workshop LoadWorkshopData(const std::filesystem::path &settings, bool pathTracing = false)
{
    using namespace DirectX;
    auto check = [](bool ok, const std::string &why)
    {
        if (!ok)
            throw std::runtime_error("Workshop: " + why);
    };
    auto bytes = ReadBinary(settings);
    auto config = Json::parse(bytes);
    check(config.at("profile") == (pathTracing ? "pathtracing-scene-1" : "workshop-scene-1"), "unsupported scene profile");
    Reader r(settings.parent_path() / config.at("model").get<std::string>());
    const auto &root = r.Get("");
    r.Check(r.Get("/asset/version") == "2.0", "/asset/version", "Expected glTF 2.0");
    for (auto key : {"animations", "skins"})
        r.Empty(root, key, "");
    r.ReadBuffers();
    Workshop result;
    std::map<std::pair<std::filesystem::path, ImageSemantic>, uint32_t> imageMap;
    std::set<std::string> ids;
    auto id = [&](const Json &j)
    {
        auto s = j.at("extras").at("id").get<std::string>();
        check(ids.insert(s).second, "duplicate ID: " + s);
        return s;
    };
    for (size_t mi = 0; mi < root.at("materials").size(); ++mi)
    {
        auto at = "/materials/" + std::to_string(mi);
        const auto &j = r.Get(at);
        const auto &pbr = j.at("pbrMetallicRoughness");
        WorkshopMaterial m;
        m.id = id(j);
        auto mode = j.value("alphaMode", std::string("OPAQUE"));
        check(mode == "OPAQUE" || mode == "MASK" || mode == "BLEND", "unknown alpha mode");
        m.alpha = mode == "MASK" ? AlphaMode::Mask : mode == "BLEND" ? AlphaMode::Blend : AlphaMode::Opaque;
        m.cutoff = r.Factor(j.value("alphaCutoff", Json(.5)), at + "/alphaCutoff");
        m.doubleSided = j.value("doubleSided", false);
        auto base = pbr.value("baseColorFactor", Json::array({1, 1, 1, 1}));
        check(base.size() == 4, "base factor size");
        m.base = {r.Factor(base[0], at), r.Factor(base[1], at), r.Factor(base[2], at), r.Factor(base[3], at)};
        m.factors = {r.Factor(pbr.value("metallicFactor", Json(1)), at),
                     r.Factor(pbr.value("roughnessFactor", Json(1)), at),
                     r.Scalar(j.at("normalTexture").value("scale", Json(1)), at),
                     r.Factor(j.at("occlusionTexture").value("strength", Json(1)), at)};
        auto emission = j.value("emissiveFactor", Json::array({0, 0, 0}));
        check(emission.size() == 3, "emissive factor size");
        float strength = 1;
        if (j.contains("extensions"))
            strength = r.Scalar(
                j["extensions"].at("KHR_materials_emissive_strength").value("emissiveStrength", Json(1)), at);
        check(strength >= 0, "negative emissive strength");
        m.emission = {r.Factor(emission[0], at) * strength, r.Factor(emission[1], at) * strength,
                      r.Factor(emission[2], at) * strength, 0};
        const std::array<std::string, 4> tex = {
            at + "/pbrMetallicRoughness/baseColorTexture", at + "/normalTexture",
            at + "/pbrMetallicRoughness/metallicRoughnessTexture", at + "/emissiveTexture"};
        for (uint32_t t = 0; t < 4; ++t)
        {
            auto loaded = r.Texture(tex[t]);
            if (!t)
                m.sampler = loaded.second;
            else
                check(loaded.second == m.sampler, "material requires a shared sampler");
            auto semantic = t == 0 || t == 3 ? ImageSemantic::Color
                            : t == 1         ? ImageSemantic::Normal
                                             : ImageSemantic::Linear;
            auto key = std::make_pair(loaded.first, semantic);
            auto [it, inserted] = imageMap.emplace(key, uint32_t(result.images.size()));
            if (inserted)
                result.images.push_back({loaded.first, semantic, 0});
            m.images[t] = it->second;
        }
        check(r.Texture(at + "/occlusionTexture").first == result.images[m.images[2]].path,
              "expected packed ORM");
        check(m.sampler==GltfSampler{},"Workshop runtime profile requires linear repeat mip sampling");
        if (m.alpha == AlphaMode::Mask)
            result.images[m.images[0]].alphaCutoff = m.base.w > 0 ? std::min(1.0f, m.cutoff / m.base.w) : 1;
        result.materials.push_back(m);
    }
    std::vector<std::vector<uint32_t>> meshes;
    for (size_t mesh = 0; mesh < root.at("meshes").size(); ++mesh)
    {
        const auto &j = root.at("meshes")[mesh];
        id(j);
        r.Empty(j, "weights", "/meshes");
        std::vector<uint32_t> references;
        for (const auto &pj : j.at("primitives"))
        {
            check(pj.value("mode", 4) == 4, "indexed TRIANGLES required");
            r.Empty(pj, "targets", "/primitives");
            WorkshopPrimitive p;
            p.id = id(pj);
            p.material = pj.at("material").get<uint32_t>();
            check(p.material < result.materials.size(), "material out of range");
            const auto &a = pj.at("attributes");
            auto pos = r.ReadAccessor(a.at("POSITION"), p.id, "VEC3", false),
                 normal = r.ReadAccessor(a.at("NORMAL"), p.id, "VEC3", false),
                 uv = r.ReadAccessor(a.at("TEXCOORD_0"), p.id, "VEC2", false),
                 tan = r.ReadAccessor(a.at("TANGENT"), p.id, "VEC4", false),
                 ind = r.ReadAccessor(pj.at("indices"), p.id, "SCALAR", true);
            check(pos.count == normal.count && pos.count == uv.count && pos.count == tan.count &&
                      ind.count % 3 == 0,
                  "attribute/index count mismatch");
            p.minimum = {FLT_MAX, FLT_MAX, FLT_MAX};
            p.maximum = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
            for (size_t v = 0; v < pos.count; ++v)
            {
                PbrVertex vertex{
                    {r.Float(pos, v, 0), r.Float(pos, v, 1), -r.Float(pos, v, 2)},
                    {r.Float(uv, v, 0), r.Float(uv, v, 1)},
                    {r.Float(normal, v, 0), r.Float(normal, v, 1), -r.Float(normal, v, 2)},
                    {r.Float(tan, v, 0), r.Float(tan, v, 1), -r.Float(tan, v, 2), -r.Float(tan, v, 3)}};
                auto n = XMLoadFloat3(&vertex.normal), t = XMLoadFloat4(&vertex.tangent);
                check(std::abs(XMVectorGetX(XMVector3Length(n)) - 1) < .002f &&
                          std::abs(XMVectorGetX(XMVector3Length(t)) - 1) < .002f &&
                          std::abs(XMVectorGetX(XMVector3Dot(n, t))) < .002f &&
                          std::abs(vertex.tangent.w) == 1,
                      "invalid normal/tangent");
                XMStoreFloat3(&p.minimum,
                              XMVectorMin(XMLoadFloat3(&p.minimum), XMLoadFloat3(&vertex.position)));
                XMStoreFloat3(&p.maximum,
                              XMVectorMax(XMLoadFloat3(&p.maximum), XMLoadFloat3(&vertex.position)));
                p.vertices.push_back(vertex);
            }
            for (size_t i = 0; i < ind.count; i += 3)
            {
                auto a0 = r.Index(ind, i), b = r.Index(ind, i + 1), c = r.Index(ind, i + 2);
                check(a0 < pos.count && b < pos.count && c < pos.count, "index out of range");
                check(p.vertices[a0].tangent.w == p.vertices[b].tangent.w &&
                          p.vertices[a0].tangent.w == p.vertices[c].tangent.w,
                      "triangle tangent signs must agree");
                auto cross = XMVector3Cross(
                    XMLoadFloat3(&p.vertices[b].position) - XMLoadFloat3(&p.vertices[a0].position),
                    XMLoadFloat3(&p.vertices[c].position) - XMLoadFloat3(&p.vertices[a0].position));
                check(XMVectorGetX(XMVector3LengthSq(cross)) > 1e-20f, "degenerate triangle");
                p.indices.insert(p.indices.end(), {a0, c, b});
            }
            references.push_back(uint32_t(result.primitives.size()));
            result.primitives.push_back(std::move(p));
        }
        meshes.push_back(std::move(references));
    }
    const auto &nodes = root.at("nodes");
    for (const auto &n : nodes)
    {
        id(n);
        for (auto key : {"skin", "weights", "matrix"})
            check(!n.contains(key), "unsupported Node feature: " + std::string(key));
    }
    auto vec = [&](const Json &v)
    {
        check(v.size() == 3, "expected vec3");
        return XMFLOAT3{r.Scalar(v[0], "/vector"), r.Scalar(v[1], "/vector"), r.Scalar(v[2], "/vector")};
    };
    for (const auto &sj : root.at("scenes"))
    {
        id(sj);
        WorkshopScene s;
        if (pathTracing) s.sun = {0, 0, 0, 0};
        s.name = sj.at("name");
        s.minimum = {FLT_MAX, FLT_MAX, FLT_MAX};
        s.maximum = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        std::set<size_t> visited;
        int sunCount = 0;
        std::function<void(size_t, XMMATRIX)> visit = [&](size_t ni, XMMATRIX parent)
        {
            check(ni < nodes.size() && visited.insert(ni).second, "node reference, cycle or multiple parent");
            const auto &n = nodes[ni];
            auto position = vec(n.value("translation", Json::array({0, 0, 0}))),
                 scale = vec(n.value("scale", Json::array({1, 1, 1})));
            auto rotation = n.value("rotation", Json::array({0, 0, 0, 1}));
            check(rotation.size() == 4, "rotation size");
            check(scale.x > 0 && std::abs(scale.x - scale.y) < 1e-5 && std::abs(scale.x - scale.z) < 1e-5,
                  "positive uniform Node scale required");
            XMFLOAT4 q{r.Scalar(rotation[0], "rotation"), r.Scalar(rotation[1], "rotation"),
                       r.Scalar(rotation[2], "rotation"), r.Scalar(rotation[3], "rotation")};
            check(std::abs(XMVectorGetX(XMVector4Length(XMLoadFloat4(&q))) - 1) < .002,
                  "rotation must be unit quaternion");
            auto rh = XMMatrixScaling(scale.x, scale.y, scale.z) *
                      XMMatrixRotationQuaternion(XMLoadFloat4(&q)) *
                      XMMatrixTranslation(position.x, position.y, position.z) * parent;
            const auto flip = XMMatrixScaling(1, 1, -1);
            auto world = flip * rh * flip;
            if (n.contains("mesh"))
            {
                auto mesh = n.at("mesh").get<size_t>();
                check(mesh < meshes.size(), "mesh reference");
                for (auto pi : meshes[mesh])
                {
                    WorkshopInstance instance;
                    instance.id = n.at("extras").at("id").get<std::string>();
                    instance.primitive = pi;
                    XMStoreFloat4x4(&instance.world, world);
                    // The central-bay tool box is the sole movable diagnostic occluder.
                    instance.movable =
                        (pathTracing && n.value("name", std::string()) == "WhiteBox") ||
                        n.value("name", std::string()).find("ToolBox_00") != std::string::npos &&
                        (s.name == "Standard" || instance.id.find("11") != std::string::npos);
                    instance.minimum = {FLT_MAX, FLT_MAX, FLT_MAX};
                    instance.maximum = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
                    const auto &p = result.primitives[pi];
                    for (int corner = 0; corner < 8; ++corner)
                    {
                        auto v =
                            XMVector3TransformCoord(XMVectorSet(corner & 1 ? p.maximum.x : p.minimum.x,
                                                                corner & 2 ? p.maximum.y : p.minimum.y,
                                                                corner & 4 ? p.maximum.z : p.minimum.z, 1),
                                                    world);
                        XMStoreFloat3(&instance.minimum, XMVectorMin(XMLoadFloat3(&instance.minimum), v));
                        XMStoreFloat3(&instance.maximum, XMVectorMax(XMLoadFloat3(&instance.maximum), v));
                    }
                    XMStoreFloat3(&s.minimum,
                                  XMVectorMin(XMLoadFloat3(&s.minimum), XMLoadFloat3(&instance.minimum)));
                    XMStoreFloat3(&s.maximum,
                                  XMVectorMax(XMLoadFloat3(&s.maximum), XMLoadFloat3(&instance.maximum)));
                    s.instances.push_back(instance);
                }
            }
            if (n.contains("extensions"))
            {
                auto li = n.at("extensions").at("KHR_lights_punctual").at("light").get<size_t>();
                const auto &light = root.at("extensions").at("KHR_lights_punctual").at("lights").at(li);
                auto color = vec(light.value("color", Json::array({1, 1, 1})));
                float intensity = r.Scalar(light.value("intensity", Json(1)), "intensity") / 683;
                check(intensity >= 0, "negative light intensity");
                auto type = light.at("type").get<std::string>();
                if (type == "point")
                {
                    WorkshopLight l;
                    XMStoreFloat4(&l.positionRange, XMVector3TransformCoord(XMVectorZero(), world));
                    l.positionRange.w = r.Scalar(light.value("range", Json(0)), "range");
                    check(l.positionRange.w >= 0, "negative range");
                    l.colorIntensity = {color.x, color.y, color.z, intensity};
                    s.lights.push_back(l);
                }
                else
                {
                    check(type == "directional" && ++sunCount == 1, "one directional light required");
                    XMStoreFloat3(&s.direction, XMVector3Normalize(XMVector3TransformNormal(
                                                    XMVectorSet(0, 0, -1, 0), world)));
                    s.sun = {color.x, color.y, color.z, intensity};
                }
            }
            if (n.contains("children"))
                for (const auto &child : n["children"])
                    visit(child.get<size_t>(), rh);
        };
        for (const auto &node : sj.at("nodes"))
            visit(node.get<size_t>(), XMMatrixIdentity());
        check((pathTracing || sunCount == 1) && !s.instances.empty(), "empty scene or missing sun");
        result.scenes.push_back(std::move(s));
    }
    auto camera = [&](const std::string &name, const Json &j)
    {
        WorkshopCamera c;
        c.name = name;
        c.position = vec(j.at("position"));
        c.target = vec(j.at("target"));
        c.position.z = -c.position.z;
        c.target.z = -c.target.z;
        c.fov = j.value("fovDegrees", 50.f);
        c.nearPlane = j.value("near", .05f);
        c.farPlane = j.value("far", 150.f);
        check(std::isfinite(c.fov) && std::isfinite(c.nearPlane) && std::isfinite(c.farPlane) && c.fov > 0 &&
                  c.fov < 180 && c.nearPlane > 0 && c.farPlane > c.nearPlane,
              "invalid camera");
        return c;
    };
    for (const auto &[name, j] : config.at("cameras").items())
        result.cameras.push_back(camera(name, j));
    if (!pathTracing || std::filesystem::exists(settings.parent_path() / "CameraPath.json"))
    {
    auto pathBytes = ReadBinary(settings.parent_path() / "CameraPath.json");
    auto path = Json::parse(pathBytes);
    for (const auto &j : path.at("frames"))
        result.path.push_back(camera("Path", j));
    }
    if (!pathTracing || config.contains("environment"))
    {
    result.environment = settings.parent_path() / config.at("environment").at("prefix").get<std::string>();
    result.environmentIntensity = config.at("environment").value("intensity", .8f);
    result.environmentYaw = config.at("environment").value("yawDegrees", 0.f);
    }
    else result.environmentIntensity = 0;
    result.exposure = config.value("exposure", 1.f);
    check(!result.cameras.empty(), "missing cameras");
    check(std::isfinite(result.exposure) && result.exposure > 0 &&
              std::isfinite(result.environmentIntensity) && result.environmentIntensity >= 0 &&
              std::isfinite(result.environmentYaw),
          "invalid exposure or environment settings");
    return result;
}
Workshop LoadWorkshop(const std::filesystem::path &settings)
{
    try
    {
        return LoadWorkshopData(settings);
    }
    catch (const Json::exception &e)
    {
        throw std::runtime_error(settings.string() + ": invalid Workshop JSON/reference: " + e.what());
    }
}
Workshop LoadPathTracingScene(const std::filesystem::path &settings)
{
    try { return LoadWorkshopData(settings, true); }
    catch (const Json::exception &e) { throw std::runtime_error(settings.string() + ": invalid PathTracing JSON/reference: " + e.what()); }
}
DirectX::XMFLOAT2 EncodeOctahedral(DirectX::XMFLOAT3 n)
{
    float sum = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
    if (sum == 0)
        throw std::runtime_error("zero normal");
    n.x /= sum;
    n.y /= sum;
    n.z /= sum;
    if (n.z < 0)
        return {(1 - std::abs(n.y)) * (n.x >= 0 ? 1.f : -1.f), (1 - std::abs(n.x)) * (n.y >= 0 ? 1.f : -1.f)};
    return {n.x, n.y};
}
DirectX::XMFLOAT3 DecodeOctahedral(DirectX::XMFLOAT2 e)
{
    DirectX::XMFLOAT3 n{e.x, e.y, 1 - std::abs(e.x) - std::abs(e.y)};
    float t = std::max(-n.z, 0.f);
    n.x += n.x >= 0 ? -t : t;
    n.y += n.y >= 0 ? -t : t;
    float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    return {n.x / length, n.y / length, n.z / length};
}
std::vector<Image> GenerateMipChain(Image base, ImageSemantic semantic, float cutoff)
{
    if (!base.width || !base.height || base.rgba.size() != size_t(base.width) * base.height * 4)
        throw std::runtime_error("Invalid mip input");
    auto decode = [](float x) { return x <= .04045f ? x / 12.92f : std::pow((x + .055f) / 1.055f, 2.4f); };
    auto encode = [](float x)
    { return x <= .0031308f ? 12.92f * x : 1.055f * std::pow(x, 1 / 2.4f) - .055f; };
    auto coverage = [&](const Image &im, float scale)
    {
        size_t count = 0;
        for (size_t i = 3; i < im.rgba.size(); i += 4)
            if (im.rgba[i] / 255.f * scale >= cutoff)
                ++count;
        return float(count) / (im.width * im.height);
    };
    float target = cutoff > 0 ? coverage(base, 1) : 0;
    std::vector<Image> chain;
    chain.push_back(std::move(base));
    while (chain.back().width > 1 || chain.back().height > 1)
    {
        const auto &src = chain.back();
        Image dst;
        dst.width = std::max(1u, src.width / 2);
        dst.height = std::max(1u, src.height / 2);
        dst.rgba.resize(size_t(dst.width) * dst.height * 4);
        for (uint32_t y = 0; y < dst.height; ++y)
            for (uint32_t x = 0; x < dst.width; ++x)
            {
                float v[4]{};
                uint32_t count = 0;
                // Area box includes the final row/column for odd source sizes.
                for (uint32_t sy = y * src.height / dst.height; sy < (y + 1) * src.height / dst.height; ++sy)
                    for (uint32_t sx = x * src.width / dst.width; sx < (x + 1) * src.width / dst.width; ++sx)
                    {
                        ++count;
                        for (int c = 0; c < 4; ++c)
                        {
                            float a = src.rgba[(size_t(sy) * src.width + sx) * 4 + c] / 255.f;
                            if (c < 3 && semantic == ImageSemantic::Color)
                                a = decode(a);
                            else if (c < 3 && semantic == ImageSemantic::Normal)
                                a = a * 2 - 1;
                            v[c] += a;
                        }
                    }
                for (auto &a : v)
                    a /= count;
                if (semantic == ImageSemantic::Normal)
                {
                    float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                    if (length < 1e-6f)
                    {
                        v[0] = v[1] = 0;
                        v[2] = 1;
                        length = 1;
                    }
                    for (int c = 0; c < 3; ++c)
                        v[c] = v[c] / length * .5f + .5f;
                }
                for (int c = 0; c < 4; ++c)
                {
                    float a = v[c];
                    if (c < 3 && semantic == ImageSemantic::Color)
                        a = encode(a);
                    dst.rgba[(size_t(y) * dst.width + x) * 4 + c] =
                        uint8_t(std::clamp(std::lround(a * 255), 0l, 255l));
                }
            }
        if (cutoff > 0)
        {
            float lo = 0, hi = 4;
            for (int i = 0; i < 16; ++i)
            {
                float mid = (lo + hi) * .5f;
                if (coverage(dst, mid) < target)
                    lo = mid;
                else
                    hi = mid;
            }
            float scale =
                std::abs(coverage(dst, lo) - target) <= std::abs(coverage(dst, hi) - target) ? lo : hi;
            for (size_t i = 3; i < dst.rgba.size(); i += 4)
                dst.rgba[i] = uint8_t(std::clamp(std::lround(dst.rgba[i] * scale), 0l, 255l));
        }
        chain.push_back(std::move(dst));
    }
    return chain;
}
} // namespace tutorial
