#pragma once
#include "MeshTiming.h"
namespace tutorial
{
struct SkinningTiming : MeshTiming
{
    std::vector<std::array<double, 2>> cpuSamples;
    uint32_t actors = 1, vertices = 0, joints = 0;
    std::string animation;
    void RecordSkin(double pose, double bounds)
    {
        if (benchmark && frame > 60 && frame <= 1260)
            cpuSamples.push_back({pose, bounds});
    }
    void Save() const
    {
        MeshTiming::Save();
        if (output.empty())
            return;
        nlohmann::json report;
        std::ifstream(output) >> report;
        report["representation"] = "Skinned Forward PBR";
        report["gBufferBytesPerPixel"] = 0;
        report["pointLights"] = 0;
        report["skinning"] = {
            {"technique", SKIN_TECHNIQUE == 1   ? "vertex"
                          : SKIN_TECHNIQUE == 2 ? "compute-cache"
                                                : "mesh"},
            {"animation", animation},
            {"actors", actors},
            {"verticesPerActor", vertices},
            {"joints", joints},
            {"cacheBytes", SKIN_TECHNIQUE == 2 ? uint64_t(actors) * vertices * 48 : 0},
            {"allocatedCacheBytes", SKIN_TECHNIQUE == 2 ? uint64_t(64) * vertices * 48 : 48},
            {"sharedSourceGeometry", true},
            {"cpuMeasurement",
             "pose and conservative bounds separately; command recording excludes waits/readback"}};
        report["passes"]["color"] = report["passes"]["geometry"];
        report["passes"].erase("geometry");
        if constexpr (SKIN_TECHNIQUE == 2)
            report["passes"]["skinning-compute"] = report["passes"]["mesh-cull"];
        for (auto name : {"lighting", "transparent", "extract", "horizontal", "vertical", "mesh-cull"})
            report["passes"].erase(name);
        for (size_t p = 0; p < 2; ++p)
        {
            std::vector<double> values;
            double sum = 0;
            for (auto v : cpuSamples)
            {
                values.push_back(v[p]);
                sum += v[p];
            }
            std::sort(values.begin(), values.end());
            if (!values.empty())
                report["passes"][p == 0 ? "pose-cpu" : "bounds-cpu"] = {
                    {"averageMs", sum / values.size()},
                    {"medianMs", values[values.size() / 2]},
                    {"p95Ms", values[std::min(values.size() - 1, size_t(values.size() * .95))]}};
        }
        std::ofstream(output) << report.dump(2);
        std::ofstream csv(output.parent_path() / (output.stem().string() + "-skin-cpu.csv"));
        csv << "frame,pose-cpu,bounds-cpu\n";
        for (size_t i = 0; i < cpuSamples.size(); ++i)
            csv << i << ',' << cpuSamples[i][0] << ',' << cpuSamples[i][1] << '\n';
    }
};
} // namespace tutorial
