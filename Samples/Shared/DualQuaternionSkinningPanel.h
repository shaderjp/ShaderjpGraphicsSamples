#pragma once
#include "MeshPanel.h"
#include "DualQuaternionSkinningData.h"
#include "SkinningTiming.h"
namespace tutorial
{
struct DualQuaternionSkinningPanel : MeshPanel
{
    bool dualQuaternion = true, testDqs = false;
    ImVec2 dqsPoint{}, twistPoint{};
    int animation = 0;
    float animationTime = 0, speed = 1, previous = 0;
    bool paused = false, loop = true;
    bool rootMotion = false, testRootMotion = false;
    double rootMotionTime = 0;
    ImVec2 rootPoint{}, stepPoint{};
    std::vector<int> animations;
    void Initialize(const Workshop &w, const Options &o, const GltfSkinnedModel &model)
    {
        auto baseOptions = o;
        baseOptions.postView.clear();
        MeshPanel::Initialize(w, baseOptions);
        // Skinning benchmarks use a fixed Hero / StressHero camera for every method.
        if (o.benchmark)
            pathEnabled = pathPlaying = false;
        forward = true;
        grid.perRoom = 0;
        grid.mode = 0;
        bloom = false;
        shadows = !o.disableShadows;
        pcf = !o.hardShadows;
        if (!o.postView.empty())
        {
            const std::wstring modes[] = {L"final",    L"base",  L"normal", L"metallic", L"roughness",
                                          L"emissive", L"depth", L"shadow", L"weights"};
            auto it = std::find(std::begin(modes), std::end(modes), o.postView);
            if (it == std::end(modes))
                throw std::runtime_error("Unknown skinning view");
            view = int(it - std::begin(modes));
        }
        renderPath = SKIN_TECHNIQUE == 6 ? 2 : 0;
        culling = SKIN_TECHNIQUE == 6 ? 1 : 0;
        if (o.meshRenderPath == L"vertex")
            renderPath = 0;
        if (o.meshRenderPath == L"mesh")
            renderPath = 1;
        if (o.meshRenderPath == L"task")
            renderPath = 2;
        if (SKIN_TECHNIQUE != 6 && renderPath != 0)
            throw std::runtime_error("Basic skinning uses indexed drawing");
        if (o.meshCulling == L"off")
            culling = 0;
        else if (o.meshCulling == L"frustum-cone")
            throw std::runtime_error("Animated normal cones are not supported");
        int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, o.animation.data(),
                                         int(o.animation.size()), nullptr, 0, nullptr, nullptr);
        if (length <= 0)
            throw std::runtime_error("Invalid animation name");
        std::string wanted(size_t(length), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, o.animation.data(), int(o.animation.size()),
                            wanted.data(), length, nullptr, nullptr);
        animation = -1;
        for (size_t i = 0; i < model.clips.size(); ++i)
        {
            animations.push_back(int(i)); // Include bind/T poses and the runtime twist diagnostic.
            if (model.clips[i].name == wanted)
                animation = int(i);
        }
        if (animation < 0)
            throw std::runtime_error("Unknown --animation");
        animationTime = o.animationTime;
        speed = o.animationSpeed;
        paused = o.pauseAnimation;
        rootMotion = o.rootMotion;
        rootMotionTime = rootMotion ? o.animationTime : 0;
        testRootMotion = o.testRootMotion;
        testDqs = o.testDqs;
        dualQuaternion = o.skinning != L"lbs";
        loop = Loops(model.clips[animation].name);
        scene = o.characters == 64 ? 1 : scene;
        if (o.camera.empty())
            camera = scene ? 1 : 0;
        if (test)
        {
            paused = true;
            animationTime = 0;
            rootMotion = false;
            rootMotionTime = 0;
        }
    }
    static bool Loops(const std::string &name)
    {
        return name != "UAL_JumpStart" && name != "UAL_JumpLand";
    }
    void Advance(float seconds, const GltfSkinnedModel &model)
    {
        float delta = std::max(0.f, seconds - previous);
        previous = seconds;
        auto duration = model.clips.at(animation).duration;
        if (!paused)
        {
            float advance = delta * speed;
            if (!loop)
                advance = std::min(advance, std::max(0.f, duration - animationTime));
            animationTime += advance;
            if (rootMotion)
                rootMotionTime += advance;
        }
        if (loop && duration > 0)
            animationTime = std::fmod(animationTime, duration);
        else
            animationTime = std::min(animationTime, duration);
    }
    void QueueTestInput()
    {
        if (testDqs)
        {
            ++frame;
            auto &io = ImGui::GetIO();
            io.AddFocusEvent(true);
            if (frame == 8 || frame == 18 || frame == 26 || frame == 34)
            {
                auto p = frame == 26 ? twistPoint : frame == 34 ? resetPoint : dqsPoint;
                io.AddMousePosEvent(p.x, p.y); io.AddMouseButtonEvent(0, true);
            }
            if (frame == 9 || frame == 19 || frame == 27 || frame == 35)
                io.AddMouseButtonEvent(0, false);
            return;
        }
        if (!testRootMotion)
        {
            MeshPanel::QueueTestInput();
            return;
        }
        ++frame;
        auto &io = ImGui::GetIO();
        io.AddFocusEvent(true);
        if (frame == 8 || frame == 10 || frame == 18 || frame == 26)
        {
            auto p = frame == 10 ? stepPoint : frame == 26 ? resetPoint : rootPoint;
            io.AddMousePosEvent(p.x, p.y);
            io.AddMouseButtonEvent(0, true);
        }
        if (frame == 9 || frame == 11 || frame == 19 || frame == 27)
            io.AddMouseButtonEvent(0, false);
    }
    std::filesystem::path TestCapture() const
    {
        if (testDqs)
        {
            const wchar_t *suffix = frame == 6 ? L"-baseline" : frame == 14 ? L"-lbs" :
                frame == 24 ? L"-dqs" : frame == 32 ? L"-twist" : frame == 40 ? L"-reset" : nullptr;
            return suffix && !screenshot.empty() ? screenshot.parent_path() /
                (screenshot.stem().wstring() + suffix + L".png") : std::filesystem::path{};
        }
        if (!testRootMotion)
            return MeshPanel::TestCapture();
        if (screenshot.empty())
            return {};
        const wchar_t *suffix = frame == 6    ? L"-baseline"
                                : frame == 14 ? L"-enabled"
                                : frame == 24 ? L"-disabled"
                                : frame == 32 ? L"-reset"
                                              : nullptr;
        return suffix ? screenshot.parent_path() / (screenshot.stem().wstring() + suffix + L".png")
                      : std::filesystem::path{};
    }
    void DrawSkin(const GltfSkinnedModel &model, const DualQuaternionSkinningData &data)
    {
        if (!visible)
            return;
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({345, 690}, ImGuiCond_FirstUseEver);
        ImGui::Begin("GPU Skinning");
        ImGui::TextUnformatted(SKIN_TECHNIQUE == 4 ? "Vertex Shader: LBS / DQS" :
                              SKIN_TECHNIQUE == 5 ? "Compute Cache: LBS / DQS" : "Mesh Shader: LBS / DQS");
        ImGui::Checkbox("Dual quaternion (DQS)", &dualQuaternion);
        auto dmin = ImGui::GetItemRectMin(), dmax = ImGui::GetItemRectMax();
        dqsPoint = {(dmin.x+dmax.x)*.5f,(dmin.y+dmax.y)*.5f};
        if (ImGui::Button("Twist check (150 deg)"))
        {
            for (int i : animations) if (model.clips[i].name == "DQS_Twist") animation = i;
            animationTime = 2; paused = true; loop = true;
            rootMotion = false; rootMotionTime = 0; scene = 0; camera = 3; navigation.Reset();
        }
        dmin = ImGui::GetItemRectMin(); dmax = ImGui::GetItemRectMax();
        twistPoint = {(dmin.x+dmax.x)*.5f,(dmin.y+dmax.y)*.5f};
        if (ImGui::BeginCombo("Animation", model.clips[animation].name.c_str()))
        {
            for (int i : animations)
                if (ImGui::Selectable(model.clips[i].name.c_str(), i == animation))
                {
                    animation = i;
                    animationTime = 0;
                    rootMotionTime = 0;
                    loop = Loops(model.clips[i].name);
                }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Paused", &paused);
        ImGui::Checkbox("Loop", &loop);
        if (ImGui::Checkbox("Root motion", &rootMotion))
            rootMotionTime = 0;
        auto rootLeft = ImGui::GetItemRectMin(), rootRight = ImGui::GetItemRectMax();
        rootPoint = {(rootLeft.x + rootRight.x) * .5f, (rootLeft.y + rootRight.y) * .5f};
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("ON: move forward using a root track reconstructed from stance-foot motion.\n"
                              "OFF: return to the original placement and animate in place.");
        ImGui::SliderFloat("Speed", &speed, 0, 3);
        if (ImGui::SliderFloat("Time", &animationTime, 0, model.clips[animation].duration))
            rootMotionTime = rootMotion ? animationTime : 0;
        auto left = ImGui::GetItemRectMin(), right = ImGui::GetItemRectMax();
        intensityPoint = {left.x + (right.x - left.x) * .7f, (left.y + right.y) * .5f};
        if (ImGui::Button("Step 1 frame"))
        {
            paused = true;
            float advance =
                loop ? 1.f / 30 : std::min(1.f / 30, model.clips[animation].duration - animationTime);
            animationTime += advance;
            if (loop)
                animationTime = std::fmod(animationTime, model.clips[animation].duration);
            if (rootMotion)
                rootMotionTime += advance;
        }
        left = ImGui::GetItemRectMin();
        right = ImGui::GetItemRectMax();
        stepPoint = {(left.x + right.x) * .5f, (left.y + right.y) * .5f};
        const char *scenes[] = {"1 character", "64 independent poses"};
        if (ImGui::Combo("Characters", &scene, scenes, 2))
        {
            camera = scene ? 1 : 0;
            navigation.Reset();
        }
        if constexpr (SKIN_TECHNIQUE == 6)
        {
            const char *paths[] = {"Vertex reference", "Mesh direct", "AS / Task"};
            ImGui::Combo("Render path", &renderPath, paths, 3);
            const char *culls[] = {"OFF", "Animated frustum"};
            ImGui::Combo("Culling", &culling, culls, 2);
            ImGui::Checkbox("Meshlet colors", &meshletView);
        }
        ImGui::Checkbox("Shadow", &shadows);
        ImGui::Checkbox("3x3 PCF", &pcf);
        ImGui::Checkbox("IBL", &ibl);
        const char *views[] = {"PBR",      "Base color", "Normal",       "Metallic", "Roughness",
                               "Emissive", "Depth",      "Shadow depth", "Weights"};
        ImGui::Combo("View", &view, views, 9);
        ImGui::Text("Pose %.3f ms / %s %.3f ms", data.poseMilliseconds,
                    SKIN_TECHNIQUE == 6 ? "Bounds" : "Instance", data.boundsMilliseconds);
        auto offset = data.rootMotion.Offset(size_t(animation), rootMotion ? rootMotionTime : 0);
        ImGui::Text("Root %.2f m / %.2f m/s", -offset.z, data.rootMotion.speeds[animation]);
        ImGui::Text("DQ conversion %.3f ms", data.conversionMilliseconds);
        ImGui::Text("DQ palette %.2f KiB", double(data.jointCount) * (scene ? 64 : 1) * 32 / 1024);
        if constexpr (SKIN_TECHNIQUE == 5)
            ImGui::Text("Vertex cache %.2f MiB", double(data.vertexCount) * (scene ? 64 : 1) * 48 / (1024*1024));
        if (ImGui::Button("Reset"))
        {
            for (int i : animations)
                if (model.clips[i].name == "UAL_Walk")
                    animation = i;
            dualQuaternion = true;
            renderPath = SKIN_TECHNIQUE == 6 ? 2 : 0;
            culling = SKIN_TECHNIQUE == 6 ? 1 : 0;
            meshletView = false;
            animationTime = 0;
            speed = 1;
            paused = test;
            loop = true;
            rootMotion = false;
            rootMotionTime = 0;
            scene = camera = 0;
            shadows = pcf = ibl = true;
            view = 0;
            navigation.Reset();
        }
        left = ImGui::GetItemRectMin();
        right = ImGui::GetItemRectMax();
        resetPoint = {(left.x + right.x) * .5f, (left.y + right.y) * .5f};
        ImGui::End();
    }
};
} // namespace tutorial
