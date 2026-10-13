#include "../Samples/Shared/MeshletData.h"
#include <iostream>
#include <map>
#include <tuple>
using namespace tutorial;
static void Require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
int wmain(int argc, wchar_t **argv)
{
    try
    {
        Require(argc == 2, "Repository path required");
        auto root = std::filesystem::path(argv[1]);
        auto w = LoadWorkshop(root / L"Assets/Scenes/Workshop/Workshop.json");
        auto data = BuildMeshletData(w);
        auto again = BuildMeshletData(w);
        Require(data.meshlets.size() == again.meshlets.size() &&
                    std::memcmp(data.meshlets.data(), again.meshlets.data(), data.meshlets.size() * sizeof(Meshlet)) ==
                        0,
                "Deterministic meshlet generation");
        Require(data.meshletVertices == again.meshletVertices && data.meshletIndices == again.meshletIndices,
                "Deterministic meshlet topology");
        size_t triangles = 0;
        for (size_t primitive = 0; primitive < w.primitives.size(); ++primitive)
        {
            const auto &source = w.primitives[primitive];
            const auto &p = data.primitives[primitive];
            std::map<std::tuple<uint32_t, uint32_t, uint32_t>, int> expected, actual;
            for (size_t t = 0; t < source.indices.size(); t += 3)
                ++expected[{source.indices[t], source.indices[t + 1], source.indices[t + 2]}];
            for (uint32_t j = 0; j < p.meshlets.y; ++j)
            {
                const auto &m = data.meshlets[p.meshlets.x + j];
                Require(m.offsets.z > 0 && m.offsets.z <= 64 && m.offsets.w > 0 && m.offsets.w <= 96, "Meshlet limits");
                for (uint32_t v = 0; v < m.offsets.z; ++v)
                {
                    auto index = data.meshletVertices.at(m.offsets.x + v);
                    Require(index < source.vertices.size(), "Meshlet vertex range");
                    const auto &position = source.vertices[index].position;
                    auto dx = position.x - m.sphere.x, dy = position.y - m.sphere.y, dz = position.z - m.sphere.z;
                    Require(std::sqrt(dx * dx + dy * dy + dz * dz) <= m.sphere.w + 1e-4f,
                            "Bounding sphere contains all vertices");
                }
                for (uint32_t t = 0; t < m.offsets.w; ++t)
                {
                    uint32_t ids[3];
                    for (uint32_t v = 0; v < 3; ++v)
                    {
                        auto local = data.meshletIndices.at(m.offsets.y + t * 3 + v);
                        Require(local < m.offsets.z, "Local triangle range");
                        ids[v] = data.meshletVertices.at(m.offsets.x + local);
                    }
                    ++actual[{ids[0], ids[1], ids[2]}];
                    ++triangles;
                }
            }
            Require(actual == expected, "Every original oriented triangle reconstructed exactly once");
        }
        auto standard = MakeMeshCandidates(w, data, 0, 1), stress = MakeMeshCandidates(w, data, 1, 1);
        Require(stress.size() == standard.size() * 9, "Stress shares geometry and has nine times the instances");
        auto opaque = MakeMeshCandidates(w, data, 0, 0);
        Require(opaque.size() < standard.size(), "MASK preset filters candidates");
        auto instances = MakeMeshInstances(w, 0, 0), moved = MakeMeshInstances(w, 0, 1);
        bool changed = false;
        for (size_t i = 0; i < instances.size(); ++i)
        {
            if (w.scenes[0].instances[i].movable)
            {
                Require(std::abs(moved[i].world._41 - instances[i].world._41 - 1) < 1e-5,
                        "Movable instance world transform");
                changed = true;
            }
            else
                Require(std::memcmp(&instances[i], &moved[i], sizeof(MeshInstance)) == 0, "Fixed instances unaffected");
        }
        Require(changed, "Movable occluder fixture");
        auto invalid = w;
        invalid.images.resize(65);
        bool rejected = false;
        try
        {
            BuildMeshletData(invalid);
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        Require(rejected, "Texture array overflow rejected");
        std::cout << "Meshlet CPU tests passed: " << data.meshlets.size() << " meshlets, " << triangles
                  << " triangles, " << standard.size() << " / " << stress.size() << " candidates\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
