#include "../Samples/Shared/PathTracingData.h"
#include <iostream>
#include <stdexcept>
using namespace tutorial;
void Require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
int main(int argc, char **argv)
{
    try {
        const std::filesystem::path root = argc > 1 ? argv[1] : ".";
        auto room = LoadPathTracingScene(root / "Assets/Scenes/PathTracing/PathTracing.json");
        Require(room.scenes.size() == 1 && room.scenes[0].sun.w == 0 && room.environment.empty() && room.path.empty(), "sunless profile");
        bool rejected = false;
        try { LoadWorkshop(root / "Assets/Scenes/PathTracing/PathTracing.json"); } catch (const std::exception &) { rejected = true; }
        Require(rejected, "legacy profile remains strict");
        auto workshop = LoadWorkshop(root / "Assets/Scenes/Workshop/Workshop.json");
        auto merged = MergePathScenes(room, workshop);
        Require(merged.scenes.size() == 3 && merged.images.size() <= RayTextureCount, "shared room/workshop assets");
        for (auto name : {L"pt-area", L"pt-transformed", L"pt-dark"}) {
            auto fixture = PathFixture(merged, name); auto instances = MakeRayInstances(fixture, 0, 1, 0);
            auto data = MakePathLights(fixture, instances);
            if (name == std::wstring(L"pt-dark")) { Require(data.count == 0 && data.lights.size() == 1, "empty light descriptor"); continue; }
            Require(data.count == 2, "rectangle has two emitter triangles");
            float expectedArea = name == std::wstring(L"pt-area") ? 2.25f : 2.25f * 1.4f * 1.4f;
            Require(std::abs(data.lights[0].originArea.w + data.lights[1].originArea.w - expectedArea) < 2e-5f, "transformed world area");
            Require(std::abs(data.lights[0].edge2Probability.w - .5f) < 1e-6f && data.lights.back().edge1Cdf.w == 1, "CDF and probabilities");
            Require(instances[1].primitive.y == 0 && instances[0].primitive.y == UINT32_MAX, "emitter hit lookup");
            for (auto &v : fixture.primitives[1].vertices) v.position = {0, 0, 0};
            Require(MakePathLights(fixture, instances).count == 0, "degenerate emitters cannot divide by zero");
        }
        std::cout << "PathTracing CPU profile, asset merge, transformed area, CDF, empty/degenerate lights PASS\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
