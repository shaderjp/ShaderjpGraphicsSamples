#pragma once
#include "SkinningData.h"
#include "DualQuaternion.h"
#include "DualQuaternionSkinningBounds.h"
namespace tutorial
{
struct DualQuaternionSkinningData : SkinningData
{
    std::vector<SkinDualQuaternion> dualQuaternions;
    std::vector<SkinDqsBounds> dqsBounds;
    bool useDualQuaternion = true;
    double conversionMilliseconds = 0;
    Workshop Build(const std::filesystem::path &path)
    {
        using namespace DirectX;
        auto workshop = SkinningData::Build(path);
        // Runtime diagnostic only: the distributed glTF and Blender actions stay intact.
        auto tpose = std::find_if(model.clips.begin(), model.clips.end(),
                                  [](const SkinClip &c) { return c.name == "Retarget_Tpose"; });
        if (tpose == model.clips.end()) throw std::runtime_error("DQS diagnostic requires Retarget_Tpose");
        auto pose = EvaluateGltfPose(model, size_t(tpose - model.clips.begin()), 0);
        SkinClip twist;
        twist.name = "DQS_Twist";
        twist.duration = 4;
        for (uint32_t node = 0; node < model.nodes.size(); ++node)
        {
            const auto &n = model.nodes[node];
            if (n.hasMatrix) continue;
            auto local = XMLoadFloat4x4(&pose.globals[node]);
            if (n.parent >= 0)
                local *= XMMatrixInverse(nullptr, XMLoadFloat4x4(&pose.globals[n.parent]));
            XMVECTOR s, r, t;
            if (!XMMatrixDecompose(&s, &r, &t, local)) throw std::runtime_error("DQS diagnostic TRS");
            XMFLOAT4 translation, rotation;
            XMStoreFloat4(&translation, t); XMStoreFloat4(&rotation, r);
            twist.channels.push_back({node, SkinChannelPath::Translation, false, {0,4}, {translation,translation}});
            twist.channels.push_back({node, SkinChannelPath::Scale, false, {0,4}, {{1,1,1,0},{1,1,1,0}}});
            SkinChannel channel{node, SkinChannelPath::Rotation};
            for (int frame = 0; frame <= 120; ++frame)
            {
                float time = float(frame) / 30;
                float angle = XMConvertToRadians(150.f) * (.5f - .5f*std::cos(XM_PI*time/2));
                // Local +Y follows the forearm/hand chain of this existing robot.
                if (n.name != "Hand.L" && n.name != "Hand.R") angle = 0;
                if (n.name == "Hand.R") angle = -angle;
                auto matrix = XMMatrixRotationY(angle) * XMMatrixRotationQuaternion(r);
                XMFLOAT4 value; XMStoreFloat4(&value, XMQuaternionRotationMatrix(matrix));
                channel.times.push_back(time); channel.values.push_back(value);
            }
            twist.channels.push_back(std::move(channel));
        }
        model.clips.push_back(std::move(twist));
        rootMotion.speeds.resize(model.clips.size(), 0);
        workshop.cameras.push_back({"Twist", {1.2f,1.8f,-1.35f}, {.65f,1.5f,0}, 30,.05f,100});
        return workshop;
    }
    void PrepareBounds(const MeshletData &data)
    {
        SkinningData::PrepareBounds(data);
        dualQuaternions.resize(palettes.size());
        dqsBounds.resize(data.meshlets.size());
        for (size_t m=0; m<data.meshlets.size(); ++m)
        {
            const auto &mesh=data.meshlets[m];
            auto &bound=dqsBounds[m];
            uint32_t base=0;
            for (size_t p=0; p<model.primitives.size(); ++p)
                if (m>=data.primitives[p].meshlets.x &&
                    m<data.primitives[p].meshlets.x+data.primitives[p].meshlets.y)
                {
                    base=data.primitives[p].offsets.x;
                    for (const auto &b:jointBounds[m])bound.joints.push_back(b.joint);
                    break;
                }
            if (bound.joints.empty())continue; // Static floor uses its original sphere.
            bound.center={mesh.sphere.x,mesh.sphere.y,mesh.sphere.z};
            for (uint32_t v=0; v<mesh.offsets.z; ++v)
            {
                uint32_t vi=base+data.meshletVertices[mesh.offsets.x+v];
                const auto &vertex=data.vertices[vi];
                auto delta=DirectX::XMVectorSubtract(DirectX::XMLoadFloat4(&vertex.positionU),
                                                      DirectX::XMLoadFloat3(&bound.center));
                bound.radius=std::max(bound.radius,DirectX::XMVectorGetX(DirectX::XMVector3Length(delta)));
                uint32_t count=0;
                for (int k=0;k<4;++k)if ((&influences[vi].weights.x)[k]>0)++count;
                bound.maxInfluences=std::max(bound.maxInfluences,count);
            }
        }
    }
    void Update(size_t clip, float seconds, bool loop, int scene, const MeshletData &data,
                std::vector<MeshInstance> &instances, bool bounds)
    {
        SkinningData::Update(clip, seconds, loop, scene, data, instances, bounds && !useDualQuaternion);
        auto start = std::chrono::steady_clock::now();
        size_t count = (scene ? 64u : 1u) * jointCount;
        for (size_t i = 0; i < count; ++i)
            dualQuaternions[i] = MakeSkinDualQuaternion(palettes[i]);
        conversionMilliseconds =
            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if (bounds && useDualQuaternion)
        {
            start=std::chrono::steady_clock::now();
            for (size_t actor=0; actor<(scene?64u:1u); ++actor)
                for (size_t m=0; m<dqsBounds.size(); ++m)
                    if (!dqsBounds[m].joints.empty())
                        spheres[actor*data.meshlets.size()+m]=BoundDqsMeshlet(dqsBounds[m],
                            std::span<const SkinDualQuaternion>(dualQuaternions).subspan(actor*jointCount,jointCount));
            boundsMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        }
    }
};
} // namespace tutorial
