#pragma once
#include "SkinnedGltf.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace tutorial
{
// The bundled CC0 clips are in-place. Reconstruct a constant forward root track
// from the backward motion of the lower support foot, without changing the asset.
struct SkinningRootMotion
{
    std::vector<float> speeds;
    void Build(const GltfSkinnedModel &model)
    {
        using namespace DirectX;
        speeds.assign(model.clips.size(), 0);
        auto find = [&](const std::string &name)
        {
            for (size_t i = 0; i < model.nodes.size(); ++i)
                if (model.nodes[i].name == name)
                    return i;
            return model.nodes.size();
        };
        const size_t feet[] = {find("Foot.L"), find("Foot.R")};
        if (feet[0] == model.nodes.size() || feet[1] == model.nodes.size())
            return;
        const size_t toes[] = {find("Toes.L"), find("Toes.R")};
        for (size_t clip = 0; clip < model.clips.size(); ++clip)
        {
            const auto &animation = model.clips[clip];
            if ((animation.name != "UAL_Walk" && animation.name != "UAL_Run") || animation.duration <= 0)
                continue;
            const auto steps = std::max(1u, uint32_t(std::ceil(animation.duration * 30)));
            const float delta = animation.duration / steps;
            std::vector<std::array<XMFLOAT3, 2>> positions(steps + 1);
            float lowest = FLT_MAX;
            for (uint32_t frame = 0; frame <= steps; ++frame)
            {
                auto pose = EvaluateGltfPose(model, clip, delta * frame);
                for (size_t side = 0; side < 2; ++side)
                {
                    auto &foot = pose.globals[feet[side]];
                    auto &toe = pose.globals[toes[side] < model.nodes.size() ? toes[side] : feet[side]];
                    positions[frame][side] = {(foot._41 + toe._41) * .5f, (foot._42 + toe._42) * .5f,
                                              (foot._43 + toe._43) * .5f};
                    lowest = std::min(lowest, positions[frame][side].y);
                }
            }
            std::vector<float> supportSpeeds;
            for (uint32_t frame = 0; frame < steps; ++frame)
            {
                float left = (positions[frame][0].y + positions[frame + 1][0].y) * .5f;
                float right = (positions[frame][1].y + positions[frame + 1][1].y) * .5f;
                size_t side = left <= right ? 0 : 1;
                // LH forward is -Z. A planted foot moving toward +Z implies forward travel.
                float speed = (positions[frame + 1][side].z - positions[frame][side].z) / delta;
                if (std::min(left, right) <= lowest + .06f && speed > .05f)
                    supportSpeeds.push_back(speed);
            }
            if (!supportSpeeds.empty())
            {
                std::sort(supportSpeeds.begin(), supportSpeeds.end());
                size_t mid = supportSpeeds.size() / 2;
                speeds[clip] = supportSpeeds.size() % 2 ? supportSpeeds[mid]
                                                        : (supportSpeeds[mid - 1] + supportSpeeds[mid]) * .5f;
            }
        }
    }
    DirectX::XMFLOAT3 Offset(size_t clip, double seconds) const
    {
        return {0, 0, float(-speeds.at(clip) * seconds)};
    }
};
} // namespace tutorial
