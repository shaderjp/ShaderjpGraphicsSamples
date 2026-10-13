#pragma once
#include "DeferredData.h"
#include <chrono>
namespace tutorial
{
struct MeshCpuTimer
{
    double &result;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~MeshCpuTimer()
    {
        result += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
};
struct MeshTiming : DeferredTiming
{
    int renderPath = 0, culling = 0;
    uint32_t meshletCount = 0, candidateCount = 0;
    double cullMilliseconds = 0, cpuMilliseconds = 0;
    std::vector<std::array<double, 2>> meshSamples;
    void RecordMesh(double cull, double cpu)
    {
        cullMilliseconds = cull;
        cpuMilliseconds = cpu;
        if (benchmark && frame > 60 && frame <= 1260)
            meshSamples.push_back({cull, cpu});
    }
    void Save() const
    {
        DeferredTiming::Save();
        if (output.empty())
            return;
        nlohmann::json report;
        std::ifstream(output) >> report;
        const char *paths[] = {"vertex", "mesh", "task", "indirect"};
        const char *culls[] = {"off", "frustum", "frustum-cone"};
        report["mesh"] = {{"renderPath", paths[renderPath]},
                          {"culling", culls[culling]},
                          {"meshoptimizer", "1.3"},
                          {"maxVertices", 64},
                          {"maxTriangles", 96},
                          {"meshlets", meshletCount},
                          {"candidates", candidateCount},
                          {"cpuMeasurement", "geometry command recording; excludes waits and GPU readback"}};
        const char *names[] = {"mesh-cull", "mesh-cpu"};
        for (size_t p = 0; p < 2; ++p)
        {
            std::vector<double> values;
            double sum = 0;
            for (const auto &sample : meshSamples)
            {
                values.push_back(sample[p]);
                sum += sample[p];
            }
            std::sort(values.begin(), values.end());
            if (!values.empty())
                report["passes"][names[p]] = {
                    {"averageMs", sum / values.size()},
                    {"medianMs", values[values.size() / 2]},
                    {"p95Ms", values[std::min(values.size() - 1, size_t(values.size() * .95))]}};
        }
        std::ofstream(output) << report.dump(2);
        std::ofstream csv(output.parent_path() / (output.stem().string() + "-mesh.csv"));
        csv << "frame,mesh-cull,mesh-cpu\n";
        for (size_t i = 0; i < meshSamples.size(); ++i)
            csv << i << ',' << meshSamples[i][0] << ',' << meshSamples[i][1] << '\n';
    }
};
} // namespace tutorial
