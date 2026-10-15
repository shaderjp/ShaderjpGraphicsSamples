#pragma once
#include "SkinningTiming.h"
namespace tutorial
{
struct DualQuaternionSkinningTiming : SkinningTiming
{
    bool dualQuaternion = true;
    std::vector<double> conversionSamples;
    void RecordSkin(double pose, double bounds, double conversion)
    {
        SkinningTiming::RecordSkin(pose,bounds);
        if (benchmark && frame > 60 && frame <= 1260) conversionSamples.push_back(conversion);
    }
    void Save() const
    {
        SkinningTiming::Save();
        if (output.empty()) return;
        nlohmann::json report; std::ifstream(output) >> report;
        report["skinning"]["technique"] = SKIN_TECHNIQUE == 4 ? "vertex" :
                                           SKIN_TECHNIQUE == 5 ? "compute-cache" : "mesh";
        report["skinning"]["deformation"] = dualQuaternion ? "dqs" : "lbs";
        report["skinning"]["matrixPaletteBytes"] = actors * joints * 64;
        report["skinning"]["dualQuaternionPaletteBytes"] = actors * joints * 32;
        report["skinning"]["cpuMeasurement"] = "same CPU palettes for both modes; DQ conversion measured separately";
        if (!conversionSamples.empty())
        {
            auto values=conversionSamples; std::sort(values.begin(),values.end());
            double sum=0; for(auto v:values)sum+=v;
            report["passes"]["dq-conversion-cpu"] = {{"averageMs",sum/values.size()},
                {"medianMs",values[values.size()/2]}, {"p95Ms",values[std::min(values.size()-1,size_t(values.size()*.95))]}};
        }
        std::ofstream(output) << report.dump(2);
    }
};
} // namespace tutorial
