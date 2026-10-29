#include "../Samples/Shared/ClusterGeometryData.h"
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
using namespace tutorial;
void Require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
std::string VertexKey(const RayVertex &v)
{
    const float values[] = {v.positionU.x, v.positionU.y, v.positionU.z, v.positionU.w,
                            v.normalV.x,   v.normalV.y,   v.normalV.z,   v.normalV.w,
                            v.tangent.x,   v.tangent.y,   v.tangent.z,   v.tangent.w};
    return std::string(reinterpret_cast<const char *>(values), sizeof(values));
}
std::string TriangleKey(std::string a, std::string b, std::string c)
{
    return std::min({a + b + c, b + c + a, c + a + b});
}
int main(int argc, char **argv)
{
    try
    {
        Require(argc == 2, "Repository path required");
        Options options;
        for (int technique : {20, 21, 22, 23})
        {
            auto w = LoadGeometryLab(options, technique, argv[1]);
            auto data = MakeClusterSceneData(w, technique);
            Require(!data.clusters.empty(), "No clusters");
            for (uint32_t primitive = 0; primitive < data.ranges.size(); ++primitive)
            {
                if (technique == 22 && primitive == 2)
                    continue;
                const auto &p = w.primitives[primitive];
                std::map<std::string, int> before, after;
                for (size_t i = 0; i < p.indices.size(); i += 3)
                {
                    std::string keys[3];
                    for (int j = 0; j < 3; ++j)
                    {
                        const auto &v = p.vertices[p.indices[i + j]];
                        keys[j] = VertexKey({{v.position.x, v.position.y, v.position.z, v.uv.x},
                                             {v.normal.x, v.normal.y, v.normal.z, v.uv.y},
                                             v.tangent});
                    }
                    ++before[TriangleKey(keys[0], keys[1], keys[2])];
                }
                const auto range = data.ranges[primitive];
                for (uint32_t k = 0; k < range.y; ++k)
                {
                    const auto &c = data.clusters[range.x + k];
                    Require(c.offsets.z <= 128 && c.offsets.w <= 256, "CLAS capacity exceeded");
                    std::set<uint32_t> unique;
                    for (uint32_t i = 0; i < c.offsets.z * 3; i += 3)
                    {
                        std::string keys[3];
                        for (int j = 0; j < 3; ++j)
                        {
                            auto ix = data.rays.indices[c.offsets.y + i + j];
                            Require(ix < c.offsets.w, "Local vertex index out of bounds");
                            unique.insert(ix);
                            keys[j] = VertexKey(data.rays.vertices[c.offsets.x + ix]);
                        }
                        ++after[TriangleKey(keys[0], keys[1], keys[2])];
                    }
                    Require(unique.size() == c.offsets.w, "UniqueVertexIndexCount is not exact");
                }
                Require(before == after, "Triangles or attributes changed during clustering");
            }
            if (technique == 22)
            {
                Require(data.topologies.size() == 33, "Template bank size changed");
                for (const auto &t : data.topologies)
                {
                    int area = 0;
                    std::set<uint32_t> unique;
                    std::set<uint32_t> boundary[4];
                    for (uint32_t i = 0; i < t.triangles * 3; i += 3)
                    {
                        int x[3], y[3];
                        for (int j = 0; j < 3; ++j)
                        {
                            auto v = data.rays.indices[t.indexOffset + i + j];
                            Require(v < 81, "Tile index out of bounds");
                            unique.insert(v);
                            x[j] = v % 9;
                            y[j] = v / 9;
                            if (y[j] == 0)
                                boundary[0].insert(x[j]);
                            if (x[j] == 8)
                                boundary[1].insert(y[j]);
                            if (y[j] == 8)
                                boundary[2].insert(x[j]);
                            if (x[j] == 0)
                                boundary[3].insert(y[j]);
                        }
                        int a = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
                        Require(a < 0, "Tile winding or degenerate triangle");
                        area -= a;
                    }
                    Require(area == 128, "Tile does not cover its full square exactly");
                    Require(unique.size() == t.uniqueVertices, "Template unique index count changed");
                    for (int edge = 0; edge < 4; ++edge)
                        Require(boundary[edge].size() == t.rate * ((t.mask & (1 << edge)) ? 2 : 1) + 1,
                                "Stitched edge is missing vertices");
                }
                for (uint32_t rate : {0u, 2u, 4u, 8u})
                {
                    SelectClusterLod(data, {0, 3, 5}, 1080, 45, int(rate));
                    for (uint32_t i = 0; i < data.tileCount; ++i)
                    {
                        const auto &c = data.clusters[data.tileFirst + i];
                        auto t = c.extra.y - data.tileFirst;
                        Require(t < 33 && c.offsets.z == data.topologies[t].triangles,
                                "LOD selected an invalid topology");
                    }
                }
            }
            if (technique == 23)
                for (const auto &scene : w.scenes)
                {
                    uint32_t side = scene.name == "Stress" ? 128 : 64;
                    std::vector<uint32_t> histogram(side / 4 * (side / 4));
                    histogram[0] = 1;
                    for (uint32_t i = 0; i < side * side; ++i)
                        ++histogram[(i % side) / 4 + (i / side) / 4 * (side / 4)];
                    Require(*std::max_element(histogram.begin(), histogram.end()) == 17,
                            "PTLAS spatial capacity changed");
                    Require(scene.instances.size() == size_t(side) * side + 1,
                            "PTLAS global capacity changed");
                }
            std::cout << "Advanced" << technique << ": CPU geometry, topology and capacity PASS\n";
        }
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
