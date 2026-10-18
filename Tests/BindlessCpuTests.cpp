#include "../Samples/Shared/BindlessData.h"
#include <iostream>
#include <set>
using namespace tutorial;
void Require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char **argv)
{
    try
    {
        Require(argc == 2, "Repository root argument required");
        std::filesystem::path root = argv[1];
        for (auto preset : {L"Mini", L"Standard", L"Stress"})
        {
            Options options;
            options.scene = root / "Assets/Scenes/BindlessGallery/BindlessGallery.json";
            options.galleryPreset = preset;
            auto d = LoadGallery(options);
            Require(d.geometries.size() == 64, "Geometry sharing");
            std::set<uint32_t> used;
            for (auto instance : d.instances)
            {
                Require(instance.ids.x < 64 && instance.ids.y < d.materials.size(), "Instance reference range");
                used.insert(instance.ids.y);
            }
            Require(used.size() == d.materialCount, "Independent material budget");
            for (auto material : d.materials)
            {
                auto image = &material.images.x, layer = &material.layers.x;
                for (uint32_t c = 0; c < 4; ++c)
                    Require(image[c] < d.images.size() && layer[c] < d.imageGroups[c].size() &&
                                d.imageGroups[c][layer[c]] == image[c],
                            "Array layer and descriptor refer to different images");
            }
            for (auto g : d.geometries)
            {
                std::multiset<std::array<uint32_t, 3>> original, rebuilt;
                auto canonical = [](std::array<uint32_t, 3> t) {
                    if (t[1] < t[0] && t[1] < t[2])
                        return std::array{t[1], t[2], t[0]};
                    if (t[2] < t[0] && t[2] < t[1])
                        return std::array{t[2], t[0], t[1]};
                    return t;
                };
                for (uint32_t t = 0; t < g.lengths.y; t += 3)
                    original.insert(canonical(
                        {d.indices[g.offsets.y + t], d.indices[g.offsets.y + t + 1], d.indices[g.offsets.y + t + 2]}));
                for (uint32_t i = 0; i < g.offsets.w; ++i)
                {
                    auto m = d.meshlets[g.offsets.z + i];
                    Require(m.offsets.z <= 64 && m.offsets.w <= 96, "Meshlet output limits");
                    for (uint32_t t = 0; t < m.offsets.w; ++t)
                    {
                        std::array<uint32_t, 3> triangle{};
                        for (uint32_t c = 0; c < 3; ++c)
                        {
                            uint32_t local = d.meshletIndices[g.lengths.w + m.offsets.y + t * 3 + c];
                            Require(local < m.offsets.z, "Meshlet local index");
                            triangle[c] = d.meshletVertices[g.lengths.z + m.offsets.x + local];
                            Require(triangle[c] < g.lengths.x, "Geometry vertex index");
                        }
                        rebuilt.insert(canonical(triangle));
                    }
                }
                Require(original == rebuilt, "Meshlet triangles/winding differ from source");
            }
            std::cout << d.preset << ": " << d.instances.size() << " objects, " << d.materials.size() << " materials, "
                      << d.images.size() << " images PASS\n";
        }
        Options independent;
        independent.scene = root / "Assets/Scenes/BindlessGallery/BindlessGallery.json";
        independent.galleryObjects = 512;
        independent.galleryMaterials = 256;
        auto d = LoadGallery(independent);
        std::set<uint32_t> used;
        for (auto i : d.instances)
            used.insert(i.ids.y);
        Require(used.size() == 256, "Materials were coupled to the saved preset");
        std::cout << "Independent counts and meshlet reconstruction PASS\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
