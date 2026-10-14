#include "SkinnedGltf.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
namespace tutorial
{
namespace
{
using J = nlohmann::json;
using namespace DirectX;
struct Reader
{
    std::filesystem::path path;
    J doc;
    std::vector<std::vector<uint8_t>> buffers;
    [[noreturn]] void Fail(const std::string &at, const std::string &why) const
    {
        throw std::runtime_error(path.string() + ": " + at + ": " + why);
    }
    void Check(bool ok, const std::string &at, const std::string &why) const
    {
        if (!ok)
            Fail(at, why);
    }
    size_t UInt(const J &v, const std::string &at) const
    {
        Check(v.is_number_integer() && (v.is_number_unsigned() || v.get<int64_t>() >= 0), at,
              "Expected unsigned integer");
        auto n = v.get<uint64_t>();
        Check(n <= UINT32_MAX, at, "Integer is too large");
        return size_t(n);
    }
    float Float(const J &v, const std::string &at) const
    {
        Check(v.is_number(), at, "Expected number");
        float f = v.get<float>();
        Check(std::isfinite(f), at, "Non-finite number");
        return f;
    }
    std::filesystem::path External(const std::string &uri) const
    {
        std::string decoded;
        auto hex = [](char c)
        {
            return c >= '0' && c <= '9'   ? c - '0'
                   : c >= 'A' && c <= 'F' ? c - 'A' + 10
                   : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                          : -1;
        };
        for (size_t i = 0; i < uri.size(); ++i)
        {
            char c = uri[i];
            if (c == '%')
            {
                Check(i + 2 < uri.size() && hex(uri[i + 1]) >= 0 && hex(uri[i + 2]) >= 0, "/buffers/uri",
                      "Invalid URI escape");
                c = char(hex(uri[i + 1]) * 16 + hex(uri[i + 2]));
                i += 2;
            }
            Check(c && c != '\\' && c != ':' && c != '?' && c != '#', "/buffers/uri",
                  "Only relative file URIs are supported");
            decoded += c;
        }
        std::filesystem::path relative(std::u8string(decoded.begin(), decoded.end()));
        Check(!relative.empty() && !relative.has_root_path(), "/buffers/uri", "Expected relative file URI");
        auto root = std::filesystem::weakly_canonical(path.parent_path());
        auto file = std::filesystem::weakly_canonical(root / relative);
        auto within = file.lexically_relative(root);
        Check(!within.empty() && *within.begin() != "..", "/buffers/uri", "URI escapes model directory");
        Check(std::filesystem::is_regular_file(file), "/buffers/uri", "Missing BIN");
        return file;
    }
    void Extensions(const J &v, const std::string &at) const
    {
        if (v.is_object())
            for (auto it = v.begin(); it != v.end(); ++it)
            {
                if (it.key() == "extensionsUsed" || it.key() == "extensionsRequired" ||
                    it.key() == "extensions")
                    Check(it.value().empty(), at, "glTF extensions are unsupported");
                else if (it.key() != "extras")
                    Extensions(it.value(), at + "/" + it.key());
            }
        else if (v.is_array())
            for (size_t i = 0; i < v.size(); ++i)
                Extensions(v[i], at + "/" + std::to_string(i));
    }
    explicit Reader(const std::filesystem::path &p) : path(std::filesystem::absolute(p))
    {
        Check(path.extension() == ".gltf", "/", "Use glTF JSON plus external BIN");
        Check(std::filesystem::is_regular_file(path) && std::filesystem::file_size(path) <= 32 * 1024 * 1024,
              "/", "Missing or oversized glTF JSON");
        doc = J::parse(ReadBinary(path));
        Check(doc.at("asset").at("version") == "2.0", "/asset/version", "Expected glTF 2.0");
        Extensions(doc, "");
        for (auto &b : doc.at("buffers"))
        {
            auto file = External(b.at("uri").get<std::string>());
            size_t size = UInt(b.at("byteLength"), "/buffers/byteLength");
            Check(size > 0 && size <= 256 * 1024 * 1024 &&
                      std::filesystem::file_size(file) <= 256 * 1024 * 1024,
                  "/buffers", "Oversized BIN");
            auto bytes = ReadBinary(file);
            Check(bytes.size() >= size, "/buffers", "Truncated BIN");
            bytes.resize(size);
            buffers.push_back(std::move(bytes));
        }
    }
    struct Data
    {
        std::vector<XMFLOAT4> values;
        std::vector<uint32_t> indices;
        uint32_t component = 0;
        bool normalized = false;
    };
    Data Access(size_t index, const std::string &type, bool integer = false, bool weights = false) const
    {
        std::string at = "/accessors/" + std::to_string(index);
        Check(index < doc.at("accessors").size(), at, "Accessor out of range");
        auto &a = doc.at("accessors")[index];
        Check(!a.contains("sparse"), at, "Sparse accessors are unsupported");
        Check(a.at("type") == type, at, "Wrong accessor type");
        size_t vi = UInt(a.at("bufferView"), at);
        Check(vi < doc.at("bufferViews").size(), at, "BufferView out of range");
        auto &view = doc.at("bufferViews")[vi];
        size_t bi = UInt(view.at("buffer"), at);
        Check(bi < buffers.size(), at, "Buffer out of range");
        size_t comp = UInt(a.at("componentType"), at);
        bool norm = a.value("normalized", false);
        Check(integer ? ((comp == 5121 || comp == 5123 || (type == "SCALAR" && comp == 5125)) && !norm)
                      : (comp == 5126 && !norm) || (weights && norm && (comp == 5121 || comp == 5123)),
              at, "Unsupported component/normalization");
        size_t width = type == "VEC4" ? 4 : type == "VEC3" ? 3 : type == "VEC2" ? 2 : type == "MAT4" ? 16 : 1;
        size_t cb = comp == 5121 ? 1 : comp == 5123 ? 2 : 4;
        size_t element = width * cb;
        size_t stride = UInt(view.value("byteStride", J(element)), at);
        Check(stride >= element && stride % cb == 0, at, "Invalid stride");
        size_t offset = UInt(view.value("byteOffset", J(0)), at), length = UInt(view.at("byteLength"), at),
               inner = UInt(a.value("byteOffset", J(0)), at), count = UInt(a.at("count"), at);
        Check((offset + inner) % cb == 0, at, "Misaligned accessor");
        Check(count > 0 && offset <= buffers[bi].size() && length <= buffers[bi].size() - offset &&
                  inner <= length && element <= length - inner &&
                  count - 1 <= (length - inner - element) / stride,
              at, "Accessor exceeds BIN range");
        Data result;
        result.component = uint32_t(comp);
        result.normalized = norm;
        result.values.resize(count * (width == 16 ? 4 : 1));
        if (integer && type == "SCALAR")
            result.indices.resize(count);
        for (size_t i = 0; i < count; ++i)
            for (size_t k = 0; k < width; ++k)
            {
                auto p = buffers[bi].data() + offset + inner + i * stride + k * cb;
                float value = 0;
                if (comp == 5126)
                    std::memcpy(&value, p, 4);
                else if (comp == 5121)
                {
                    value = float(*p);
                    if (!result.indices.empty())
                        result.indices[i] = *p;
                }
                else if (comp == 5123)
                {
                    uint16_t u;
                    std::memcpy(&u, p, 2);
                    value = float(u);
                    if (!result.indices.empty())
                        result.indices[i] = u;
                }
                else
                {
                    uint32_t u;
                    std::memcpy(&u, p, 4);
                    value = float(u);
                    if (!result.indices.empty())
                        result.indices[i] = u;
                }
                if (norm)
                    value /= comp == 5121 ? 255.f : 65535.f;
                Check(std::isfinite(value), at, "Non-finite accessor value");
                (&result.values[i * (width == 16 ? 4 : 1) + k / 4].x)[k % 4] = value;
            }
        return result;
    }
    XMFLOAT4 Vec(const J &j, size_t width, const std::string &at) const
    {
        Check(j.is_array() && j.size() == width, at, "Wrong vector size");
        XMFLOAT4 v{};
        for (size_t i = 0; i < width; ++i)
            (&v.x)[i] = Float(j[i], at);
        return v;
    }
};
XMFLOAT4X4 MatrixLH(XMMATRIX matrix)
{
    XMFLOAT4X4 result;
    auto reflection = XMMatrixScaling(1, 1, -1);
    XMStoreFloat4x4(&result, reflection * matrix * reflection);
    return result;
}
XMFLOAT4 NormalizeQuat(XMFLOAT4 q)
{
    float len = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (len < 1e-10f)
        throw std::runtime_error("Zero quaternion");
    XMStoreFloat4(&q, XMQuaternionNormalize(XMLoadFloat4(&q)));
    return q;
}
} // namespace
GltfSkinnedModel LoadGltfSkinned(const std::filesystem::path &path)
{
    try
    {
        Reader r(path);
        auto &d = r.doc;
        GltfSkinnedModel result;
        for (size_t i = 0; i < d.at("materials").size(); ++i)
        {
            auto &m = d["materials"][i];
            SkinMaterial out;
            out.name = m.value("name", "Material" + std::to_string(i));
            auto p = m.value("pbrMetallicRoughness", J::object());
            for (auto key : {"baseColorTexture", "metallicRoughnessTexture"})
                r.Check(!p.contains(key), "/materials", "This skinning profile uses constant PBR materials");
            for (auto key : {"normalTexture", "occlusionTexture", "emissiveTexture"})
                r.Check(!m.contains(key), "/materials", "This skinning profile uses constant PBR materials");
            out.base =
                r.Vec(p.value("baseColorFactor", J::array({1, 1, 1, 1})), 4, "/materials/baseColorFactor");
            out.metallic = r.Float(p.value("metallicFactor", J(1)), "/materials/metallic");
            out.roughness = r.Float(p.value("roughnessFactor", J(1)), "/materials/roughness");
            auto e = r.Vec(m.value("emissiveFactor", J::array({0, 0, 0})), 3, "/materials/emissive");
            out.emission = {e.x, e.y, e.z};
            std::string mode = m.value("alphaMode", "OPAQUE");
            r.Check(mode == "OPAQUE" || mode == "MASK", "/materials/alphaMode", "BLEND is unsupported");
            out.mask = mode == "MASK";
            out.cutoff = r.Float(m.value("alphaCutoff", J(.5)), "/materials/alphaCutoff");
            out.doubleSided = m.value("doubleSided", false);
            r.Check(out.metallic >= 0 && out.metallic <= 1 && out.roughness >= 0 && out.roughness <= 1 &&
                        out.cutoff >= 0 && out.cutoff <= 1,
                    "/materials", "PBR factor out of range");
            for (int k = 0; k < 4; ++k)
                r.Check((&out.base.x)[k] >= 0 && (&out.base.x)[k] <= 1, "/materials",
                        "Base color out of range");
            r.Check(e.x >= 0 && e.y >= 0 && e.z >= 0, "/materials", "Negative emission");
            result.materials.push_back(out);
        }
        auto &nodes = d.at("nodes");
        r.Check(nodes.size() <= 4096, "/nodes", "Tutorial profile supports at most 4096 nodes");
        result.nodes.resize(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            auto &n = nodes[i];
            auto &o = result.nodes[i];
            o.name = n.value("name", "Node" + std::to_string(i));
            if (n.contains("mesh"))
                o.mesh = int(r.UInt(n["mesh"], "/nodes/mesh"));
            if (n.contains("skin"))
                o.skin = int(r.UInt(n["skin"], "/nodes/skin"));
            if (n.contains("matrix"))
            {
                auto &a = n["matrix"];
                r.Check(a.is_array() && a.size() == 16, "/nodes/matrix", "Expected MAT4");
                XMFLOAT4X4 m;
                for (size_t k = 0; k < 16; ++k)
                    (&m._11)[k] = r.Float(a[k], "/nodes/matrix");
                o.matrix = MatrixLH(XMLoadFloat4x4(&m));
                o.hasMatrix = true;
                r.Check(!n.contains("translation") && !n.contains("rotation") && !n.contains("scale"),
                        "/nodes", "Matrix and TRS are exclusive");
            }
            else
            {
                auto t = r.Vec(n.value("translation", J::array({0, 0, 0})), 3, "/nodes/translation"),
                     s = r.Vec(n.value("scale", J::array({1, 1, 1})), 3, "/nodes/scale"),
                     q = r.Vec(n.value("rotation", J::array({0, 0, 0, 1})), 4, "/nodes/rotation");
                o.translation = {t.x, t.y, -t.z};
                o.scale = {s.x, s.y, s.z};
                o.rotation = NormalizeQuat({-q.x, -q.y, q.z, q.w});
            }
            r.Check(!n.contains("weights"), "/nodes/weights", "Morphs are unsupported");
            for (auto &child : n.value("children", J::array()))
            {
                size_t c = r.UInt(child, "/nodes/children");
                r.Check(c < nodes.size() && c != i && result.nodes[c].parent == -1, "/nodes/children",
                        "Invalid child/multiple parent");
                result.nodes[c].parent = int(i);
            }
        }
        std::vector<int> state(nodes.size());
        std::function<void(size_t, size_t)> visit = [&](size_t n, size_t depth)
        {
            r.Check(depth <= 256, "/nodes", "Hierarchy exceeds tutorial depth limit");
            r.Check(state[n] != 1, "/nodes", "Hierarchy cycle");
            if (state[n] == 2)
                return;
            state[n] = 1;
            int p = result.nodes[n].parent;
            if (p >= 0)
                visit(size_t(p), depth + 1);
            state[n] = 2;
        };
        for (size_t i = 0; i < nodes.size(); ++i)
            visit(i, 0);
        auto scene = r.UInt(d.value("scene", J(0)), "/scene");
        r.Check(scene < d.at("scenes").size(), "/scene", "Scene out of range");
        std::vector<bool> active(nodes.size());
        std::function<void(size_t)> mark = [&](size_t n)
        {
            r.Check(n < nodes.size(), "/scenes/nodes", "Scene node out of range");
            if (active[n])
                return;
            active[n] = true;
            for (auto &child : nodes[n].value("children", J::array()))
                mark(r.UInt(child, "/nodes/children"));
        };
        for (auto &root : d["scenes"][scene].at("nodes"))
            mark(r.UInt(root, "/scenes/nodes"));
        for (auto &skin : d.at("skins"))
        {
            SkinDefinition out;
            std::set<uint32_t> unique;
            if (skin.contains("skeleton"))
                r.Check(r.UInt(skin["skeleton"], "/skins/skeleton") < nodes.size(), "/skins/skeleton",
                        "Skeleton node out of range");
            for (auto &joint : skin.at("joints"))
            {
                auto n = r.UInt(joint, "/skins/joints");
                r.Check(n < nodes.size() && unique.insert(uint32_t(n)).second, "/skins/joints",
                        "Invalid or duplicate joint");
                out.joints.push_back(uint32_t(n));
            }
            r.Check(!out.joints.empty(), "/skins/joints", "Empty skin");
            if (skin.contains("inverseBindMatrices"))
            {
                auto a = r.Access(r.UInt(skin["inverseBindMatrices"], "/skins/inverseBindMatrices"), "MAT4");
                r.Check(a.values.size() == out.joints.size() * 4, "/skins", "Inverse bind count mismatch");
                for (size_t i = 0; i < out.joints.size(); ++i)
                {
                    XMFLOAT4X4 m;
                    std::memcpy(&m, a.values.data() + i * 4, 64);
                    out.inverseBind.push_back(MatrixLH(XMLoadFloat4x4(&m)));
                }
            }
            else
            {
                XMFLOAT4X4 m;
                XMStoreFloat4x4(&m, XMMatrixIdentity());
                out.inverseBind.assign(out.joints.size(), m);
            }
            result.skins.push_back(std::move(out));
        }
        for (size_t ni = 0; ni < nodes.size(); ++ni)
        {
            auto &node = result.nodes[ni];
            if (node.mesh < 0 || !active[ni])
                continue;
            r.Check(node.skin >= 0 && size_t(node.skin) < result.skins.size(), "/nodes/skin",
                    "Expected valid skin on mesh node");
            r.Check(size_t(node.mesh) < d.at("meshes").size(), "/nodes/mesh", "Mesh out of range");
            auto &mesh = d["meshes"][node.mesh];
            r.Check(!mesh.contains("weights"), "/meshes", "Morphs unsupported");
            for (auto &p : mesh.at("primitives"))
            {
                r.Check(p.value("mode", 4) == 4 && !p.contains("targets"), "/meshes/primitives",
                        "Only non-morph TRIANGLES are supported");
                auto &a = p.at("attributes");
                for (auto it = a.begin(); it != a.end(); ++it)
                    r.Check(it.key() == "POSITION" || it.key() == "NORMAL" || it.key() == "TANGENT" ||
                                it.key() == "TEXCOORD_0" || it.key() == "JOINTS_0" || it.key() == "WEIGHTS_0",
                            "/meshes/attributes", "Unsupported vertex attribute/extra influence set");
                auto access =
                    [&](const char *name, const char *type, bool integer = false, bool weights = false)
                { return r.Access(r.UInt(a.at(name), "/attributes"), type, integer, weights).values; };
                auto positions = access("POSITION", "VEC3"), normals = access("NORMAL", "VEC3"),
                     joints = access("JOINTS_0", "VEC4", true),
                     weights = access("WEIGHTS_0", "VEC4", false, true);
                std::vector<XMFLOAT4> uv, tan;
                if (a.contains("TEXCOORD_0"))
                    uv = access("TEXCOORD_0", "VEC2");
                if (a.contains("TANGENT"))
                    tan = access("TANGENT", "VEC4");
                size_t count = positions.size();
                r.Check(normals.size() == count && joints.size() == count && weights.size() == count &&
                            (uv.empty() || uv.size() == count) && (tan.empty() || tan.size() == count),
                        "/attributes", "Attribute count mismatch");
                SkinPrimitive out;
                out.node = uint32_t(ni);
                out.skin = uint32_t(node.skin);
                out.material = uint32_t(r.UInt(p.at("material"), "/primitives/material"));
                r.Check(out.material < result.materials.size(), "/materials", "Material out of range");
                out.vertices.resize(count);
                for (size_t i = 0; i < count; ++i)
                {
                    auto &v = out.vertices[i];
                    auto pos = positions[i], n = normals[i];
                    auto nvec = XMVectorSet(n.x, n.y, -n.z, 0);
                    r.Check(XMVectorGetX(XMVector3Length(nvec)) > 1e-6f, "/NORMAL", "Zero normal");
                    XMStoreFloat4(&v.normalV, XMVector3Normalize(nvec));
                    v.positionU = {pos.x, pos.y, -pos.z, uv.empty() ? 0 : uv[i].x};
                    v.normalV.w = uv.empty() ? 0 : uv[i].y;
                    if (tan.empty())
                    {
                        auto axis =
                            std::abs(v.normalV.y) < .95f ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(1, 0, 0, 0);
                        XMStoreFloat4(&v.tangent,
                                      XMVector3Normalize(XMVector3Cross(axis, XMLoadFloat4(&v.normalV))));
                        v.tangent.w = -1;
                    }
                    else
                    {
                        v.tangent = {tan[i].x, tan[i].y, -tan[i].z, -tan[i].w};
                        r.Check(std::abs(std::abs(v.tangent.w) - 1) < 1e-4f, "/TANGENT",
                                "Invalid handedness");
                    }
                    float sum = 0;
                    for (int k = 0; k < 4; ++k)
                    {
                        float j = (&joints[i].x)[k], w = (&weights[i].x)[k];
                        r.Check(j >= 0 && j < result.skins[out.skin].joints.size(), "/JOINTS_0",
                                "Joint index out of skin range");
                        r.Check(w >= 0, "/WEIGHTS_0", "Negative weight");
                        (&v.joints.x)[k] = uint32_t(j);
                        (&v.weights.x)[k] = w;
                        sum += w;
                    }
                    r.Check(sum > 1e-6f && std::abs(sum - 1) < .02f, "/WEIGHTS_0",
                            "Weights must have a positive normalized sum");
                    for (int k = 0; k < 4; ++k)
                        (&v.weights.x)[k] /= sum;
                }
                auto idx = r.Access(r.UInt(p.at("indices"), "/indices"), "SCALAR", true).indices;
                r.Check(idx.size() % 3 == 0, "/indices", "Triangle count mismatch");
                for (auto x : idx)
                {
                    r.Check(x < count, "/indices", "Index out of vertex range");
                    out.indices.push_back(x);
                }
                for (size_t i = 0; i < out.indices.size(); i += 3)
                    std::swap(out.indices[i + 1], out.indices[i + 2]);
                result.primitives.push_back(std::move(out));
            }
        }
        std::set<std::string> clipNames;
        for (auto &clip : d.value("animations", J::array()))
        {
            SkinClip out;
            out.name = clip.value("name", "Animation" + std::to_string(result.clips.size()));
            r.Check(clipNames.insert(out.name).second, "/animations", "Duplicate clip name");
            std::set<std::pair<uint32_t, std::string>> targets;
            for (auto &c : clip.at("channels"))
            {
                SkinChannel channel;
                channel.node = uint32_t(r.UInt(c.at("target").at("node"), "/animations/target"));
                r.Check(channel.node < nodes.size() && !result.nodes[channel.node].hasMatrix, "/animations",
                        "Invalid or matrix-authored animation node");
                auto target = c["target"].at("path").get<std::string>();
                r.Check(target == "translation" || target == "rotation" || target == "scale",
                        "/animations/path", "Morph animation unsupported");
                r.Check(targets.insert({channel.node, target}).second, "/animations",
                        "Duplicate animation target");
                channel.path = target == "rotation" ? SkinChannelPath::Rotation
                               : target == "scale"  ? SkinChannelPath::Scale
                                                    : SkinChannelPath::Translation;
                auto si = r.UInt(c.at("sampler"), "/animations/sampler");
                r.Check(si < clip.at("samplers").size(), "/animations/sampler", "Sampler out of range");
                auto &s = clip["samplers"][si];
                auto mode = s.value("interpolation", "LINEAR");
                r.Check(mode == "LINEAR" || mode == "STEP", "/animations/interpolation",
                        "CUBICSPLINE unsupported");
                channel.step = mode == "STEP";
                auto times = r.Access(r.UInt(s.at("input"), "/animations/input"), "SCALAR").values;
                channel.values = r.Access(r.UInt(s.at("output"), "/animations/output"),
                                          target == "rotation" ? "VEC4" : "VEC3")
                                     .values;
                r.Check(times.size() == channel.values.size(), "/animations", "Key count mismatch");
                for (size_t i = 0; i < times.size(); ++i)
                {
                    r.Check(times[i].x >= 0 && (i == 0 || times[i].x > times[i - 1].x), "/animations/input",
                            "Keys must increase strictly");
                    channel.times.push_back(times[i].x);
                    auto &v = channel.values[i];
                    if (target == "rotation")
                        v = NormalizeQuat({-v.x, -v.y, v.z, v.w});
                    else if (target == "translation")
                        v.z = -v.z;
                }
                out.duration = std::max(out.duration, channel.times.back());
                out.channels.push_back(std::move(channel));
            }
            result.clips.push_back(std::move(out));
        }
        r.Check(!result.primitives.empty() && !result.skins.empty(), "/", "No skinned geometry");
        return result;
    }
    catch (const nlohmann::json::exception &e)
    {
        throw std::runtime_error(path.string() + ": invalid skinned glTF: " + e.what());
    }
}
SkinPose EvaluateGltfPose(const GltfSkinnedModel &model, size_t clip, float seconds)
{
    using namespace DirectX;
    if (!std::isfinite(seconds))
        throw std::runtime_error("Non-finite animation time");
    auto nodes = model.nodes;
    if (clip != SkinRestPose)
        for (auto &c : model.clips.at(clip).channels)
        {
            auto it = std::upper_bound(c.times.begin(), c.times.end(), seconds);
            size_t i = it == c.times.begin() ? 0 : size_t(it - c.times.begin() - 1);
            size_t j = std::min(i + 1, c.times.size() - 1);
            float alpha = c.step || i == j
                              ? 0
                              : std::clamp((seconds - c.times[i]) / (c.times[j] - c.times[i]), 0.f, 1.f);
            auto v = c.path == SkinChannelPath::Rotation
                         ? XMQuaternionSlerp(XMLoadFloat4(&c.values[i]), XMLoadFloat4(&c.values[j]), alpha)
                         : XMVectorLerp(XMLoadFloat4(&c.values[i]), XMLoadFloat4(&c.values[j]), alpha);
            auto &n = nodes[c.node];
            if (c.path == SkinChannelPath::Rotation)
                XMStoreFloat4(&n.rotation, XMQuaternionNormalize(v));
            else if (c.path == SkinChannelPath::Translation)
                XMStoreFloat3(&n.translation, v);
            else
                XMStoreFloat3(&n.scale, v);
        }
    SkinPose pose;
    pose.globals.resize(nodes.size());
    std::vector<bool> done(nodes.size());
    std::function<void(size_t)> visit = [&](size_t i)
    {
        if (done[i])
            return;
        auto &n = nodes[i];
        auto local = n.hasMatrix ? XMLoadFloat4x4(&n.matrix)
                                 : XMMatrixScaling(n.scale.x, n.scale.y, n.scale.z) *
                                       XMMatrixRotationQuaternion(XMLoadFloat4(&n.rotation)) *
                                       XMMatrixTranslation(n.translation.x, n.translation.y, n.translation.z);
        if (n.parent >= 0)
        {
            visit(size_t(n.parent));
            local *= XMLoadFloat4x4(&pose.globals[n.parent]);
        }
        XMStoreFloat4x4(&pose.globals[i], local);
        done[i] = true;
    };
    for (size_t i = 0; i < nodes.size(); ++i)
        visit(i);
    return pose;
}
std::vector<DirectX::XMFLOAT4X4> MakeSkinPalette(const GltfSkinnedModel &model, const SkinPose &pose,
                                                 size_t primitive)
{
    using namespace DirectX;
    auto &p = model.primitives.at(primitive);
    auto &skin = model.skins.at(p.skin);
    auto mesh = XMLoadFloat4x4(&pose.globals.at(p.node));
    if (std::abs(XMVectorGetX(XMMatrixDeterminant(mesh))) < 1e-8f)
        throw std::runtime_error("Singular mesh transform");
    auto inv = XMMatrixInverse(nullptr, mesh);
    std::vector<XMFLOAT4X4> result(skin.joints.size());
    for (size_t i = 0; i < result.size(); ++i)
        XMStoreFloat4x4(&result[i], XMLoadFloat4x4(&skin.inverseBind[i]) *
                                        XMLoadFloat4x4(&pose.globals.at(skin.joints[i])) * inv);
    return result;
}
SkinDeformedVertex DeformSkinVertex(const SkinVertex &v, const std::vector<DirectX::XMFLOAT4X4> &palette)
{
    using namespace DirectX;
    XMVECTOR p = XMVectorZero(), n = p, t = p;
    for (int i = 0; i < 4; ++i)
    {
        auto m = XMLoadFloat4x4(&palette.at((&v.joints.x)[i]));
        float w = (&v.weights.x)[i];
        p += XMVector3TransformCoord(XMLoadFloat4(&v.positionU), m) * w;
        n += XMVector3TransformNormal(XMLoadFloat4(&v.normalV), m) * w;
        t += XMVector3TransformNormal(XMLoadFloat4(&v.tangent), m) * w;
    }
    n = XMVector3Normalize(n);
    t = XMVector3Normalize(t - n * XMVectorGetX(XMVector3Dot(n, t)));
    SkinDeformedVertex out;
    XMStoreFloat4(&out.positionU, p);
    XMStoreFloat4(&out.normalV, n);
    XMStoreFloat4(&out.tangent, t);
    out.positionU.w = v.positionU.w;
    out.normalV.w = v.normalV.w;
    out.tangent.w = v.tangent.w;
    return out;
}
} // namespace tutorial
