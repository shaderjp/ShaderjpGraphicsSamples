#pragma once
#include "MeshPanel.h"
#include "SkinningData.h"
#include "SkinningTiming.h"
namespace tutorial
{
struct SkinningPanel : MeshPanel
{
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
        renderPath = SKIN_TECHNIQUE == 3 ? 2 : 0;
        culling = SKIN_TECHNIQUE == 3 ? 1 : 0;
        if (o.meshRenderPath == L"mesh")
            renderPath = 1;
        if (o.meshRenderPath == L"task")
            renderPath = 2;
        if (SKIN_TECHNIQUE != 3 && renderPath != 0)
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
            if (model.clips[i].name.starts_with("UAL_"))
                animations.push_back(int(i));
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
    void DrawSkin(const GltfSkinnedModel &model, const SkinningData &data)
    {
        if (!visible)
            return;
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({325, 580}, ImGuiCond_FirstUseEver);
        ImGui::Begin("GPU Skinning");
        ImGui::Text("%s", SKIN_TECHNIQUE == 1   ? "Vertex Shader"
                          : SKIN_TECHNIQUE == 2 ? "Compute Cache"
                                                : "Mesh Shader");
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
        if constexpr (SKIN_TECHNIQUE == 3)
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
        ImGui::Text("Pose %.3f ms / Bounds %.3f ms", data.poseMilliseconds, data.boundsMilliseconds);
        auto offset = data.rootMotion.Offset(size_t(animation), rootMotion ? rootMotionTime : 0);
        ImGui::Text("Root %.2f m / %.2f m/s", -offset.z, data.rootMotion.speeds[animation]);
        ImGui::Text("Cache %.2f MiB", SKIN_TECHNIQUE == 2
                                          ? double(data.vertexCount) * (scene ? 64 : 1) * 48 / (1024 * 1024)
                                          : 0);
        if (ImGui::Button("Reset"))
        {
            for (int i : animations)
                if (model.clips[i].name == "UAL_Walk")
                    animation = i;
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
