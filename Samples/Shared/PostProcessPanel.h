#pragma once
#include "CameraNavigation.h"
#include "PostProcessData.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
namespace tutorial
{
struct PostProcessPanel
{
    CameraNavigation navigation;
    int postView = 0, particleCount = 16384, testImage = 0;
    float threshold = 1, strength = .15f, gravity = 3, fixedDelta = 0;
    bool bloom = true, paused = false, stepRequested = false, resetRequested = true, captureGpu = false,
         postTest = false;
    ImVec2 stepPoint{}, pausePoint{};
    int stage = 10, environmentIndex = 0, view = 0;
    float environmentIntensity = 1, yaw = 0, directIntensity = 3, exposure = 1, bias = .001f, occluder = 0;
    bool shadows = true, pcf = true, visible = true, test = false;
    uint32_t frame = 0;
    std::filesystem::path screenshot;
    ImVec2 intensityPoint{}, resetPoint{};
    float initialIntensity = 3, initialExposure = 1;
    void Reset()
    {
        navigation.Reset();
        postView = 0;
        threshold = 1;
        strength = .15f;
        gravity = 3;
        particleCount = 16384;
        bloom = true;
        paused = postTest || test;
        stepRequested = false;
        resetRequested = true;
        environmentIndex = 0;
        view = stage == 10 ? 1 : 0;
        environmentIntensity = 1;
        yaw = 0;
        directIntensity = initialIntensity;
        exposure = initialExposure;
        bias = .001f;
        occluder = 0;
        shadows = pcf = true;
    }
    uint32_t SceneLeft(uint32_t width) const
    {
        return visible ? std::min(350u, width / 2) : 0;
    }
    void Initialize(const LightingScene &s, const Options &o, int technique)
    {
        navigation.test = o.testCamera;
        stage = technique;
        postTest = o.testPost;
        captureGpu = o.captureGpu;
        test = o.testUi;
        initialIntensity = s.intensity;
        initialExposure = s.exposure;
        visible = !o.hideUi;
        Reset();
        test = o.testUi;
        screenshot = o.screenshot;
        occluder = o.occluderOffset;
        particleCount = int(o.particleCount);
        paused = o.pauseParticles || postTest || test;
        fixedDelta = o.fixedDelta;
        gravity = o.gravity;
        threshold = o.bloomThreshold;
        strength = o.bloomStrength;
        const std::array<std::wstring, 7> postNames = {L"final", L"grayscale", L"extract", L"horizontal",
                                                       L"bloom", L"monitor",   L"input"};
        if (!o.postView.empty())
        {
            auto it = std::find(postNames.begin(), postNames.end(), o.postView);
            if (it == postNames.end())
                throw std::runtime_error("Unknown --post-view");
            postView = int(it - postNames.begin());
        }
        if (stage == 14 && postView >= 2 && postView <= 4)
            throw std::runtime_error("Bloom display requires Sample 15");
        if (!o.testImage.empty())
        {
            const std::array<std::wstring, 3> names = {L"black", L"constant", L"impulse"};
            auto it = std::find(names.begin(), names.end(), o.testImage);
            if (it == names.end())
                throw std::runtime_error("--test-image: black / constant / impulse");
            testImage = 1 + int(it - names.begin());
        }
        if (postTest && (stage != 16 || o.frames < 36 || !visible))
            throw std::runtime_error("--test-post requires Sample 16, visible UI, 36 frames");
        if (o.environment == L"room")
            environmentIndex = 1;
        else if (o.environment == L"orientation")
            environmentIndex = 2;
        else if (!o.environment.empty() && o.environment != L"studio")
            throw std::runtime_error("--environment: studio / room / orientation");
        const std::array<std::wstring, 9> names = {L"combined",  L"mirror",   L"direct",
                                                   L"diffuse",   L"specular", L"shadow-depth",
                                                   L"no-shadow", L"hard",     L"pcf"};
        if (!o.shading.empty())
        {
            const auto i = std::find(names.begin(), names.end(), o.shading);
            if (i == names.end())
                throw std::runtime_error("Unknown LightingLab display mode");
            view = int(i - names.begin());
        }
        if ((stage < 11 && view == 3) || (stage < 12 && view == 4) || (stage < 13 && view >= 5))
            throw std::runtime_error("Display mode requires a later sample");
        if (environmentIndex == 2)
            view = 1;
        if (test && (o.frames < 36 || !visible))
            throw std::runtime_error("--test-ui requires visible UI and --frames >= 36");
    }
    void QueueTestInput()
    {
        ++frame;
        if (!test && !postTest)
            return;
        auto &io = ImGui::GetIO();
        io.AddFocusEvent(true);
        if (frame == 8 || frame == 28 || frame == 18)
        {
            auto p = frame == 18 ? resetPoint
                     : postTest  ? (frame == 8 ? stepPoint : pausePoint)
                                 : intensityPoint;
            io.AddMousePosEvent(p.x, p.y);
            io.AddMouseButtonEvent(0, true);
        }
        if (frame == 9 || frame == 19 || frame == 29)
            io.AddMouseButtonEvent(0, false);
    }
    std::filesystem::path TestCapture() const
    {
        if ((!test && !postTest) || screenshot.empty())
            return {};
        const wchar_t *s = frame == 6    ? L"-baseline"
                           : frame == 14 ? L"-edited"
                           : frame == 24 ? L"-reset"
                                         : nullptr;
        return s ? screenshot.parent_path() / (screenshot.stem().wstring() + s + L".png")
                 : std::filesystem::path{};
    }
    void Draw(uint32_t width, uint32_t height, const GltfModel &, const char *api)
    {
        if (!visible)
            return;
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({float(SceneLeft(width)) - 20, float(height) - 20}, ImGuiCond_Always);
        ImGui::Begin("Lighting Lab", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushItemWidth(150);
        ImGui::Text("%s / Sample %d", api, stage);
        const char *envs[] = {"Studio", "Room", "Orientation"};
        ImGui::Combo("Environment", &environmentIndex, envs, 3);
        const float environmentWidth = std::clamp(float(SceneLeft(width)) - 190, 40.0f, 160.0f);
        ImGui::SetNextItemWidth(environmentWidth);
        ImGui::SliderFloat("Environment intensity", &environmentIntensity, 0, 4);
        auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        intensityPoint = {a.x + environmentWidth * .85f, (a.y + b.y) * .5f};
        ImGui::SliderFloat("Environment yaw", &yaw, -180, 180);
        ImGui::SliderFloat("Direct intensity", &directIntensity, 0, 8);
        ImGui::SliderFloat("Exposure", &exposure, .1f, 4);
        const char *views[] = {"Combined",         "Ideal mirror",      "Direct only",
                               "Diffuse IBL only", "Specular IBL only", "Shadow depth",
                               "No shadow",        "Hard shadow",       "3x3 PCF"};
        ImGui::Combo("View", &view, views, stage == 10 ? 3 : stage == 11 ? 4 : stage == 12 ? 5 : 9);
        if (environmentIndex == 2)
        {
            view = 1;
            ImGui::TextUnformatted("Orientation: ideal mirror diagnostic.");
        }
        if (stage >= 13)
        {
            ImGui::Checkbox("Shadow enabled", &shadows);
            ImGui::Checkbox("3x3 PCF", &pcf);
            ImGui::SliderFloat("Receiver bias", &bias, 0, .01f, "%.4f");
            ImGui::SliderFloat("Occluder X", &occluder, -1.5f, 1.5f);
        }
        const char *postViews[] = {"Final", "Grayscale",      "Extract",  "Horizontal blur",
                                   "Bloom", "Monitor camera", "HDR input"};
        ImGui::Combo("Post view", &postView, postViews, 7);
        if (stage == 14 && postView >= 2 && postView <= 4)
            postView = 0;
        if (stage >= 15)
        {
            ImGui::Checkbox("Bloom enabled", &bloom);
            ImGui::SliderFloat("Threshold", &threshold, 0, 10);
            ImGui::SliderFloat("Bloom strength", &strength, 0, 2);
        }
        if (stage >= 16)
        {
            ImGui::Checkbox("Pause particles", &paused);
            auto p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
            pausePoint = {(p0.x + p1.x) * .5f, (p0.y + p1.y) * .5f};
            if (ImGui::Button("Step particles"))
                stepRequested = true;
            p0 = ImGui::GetItemRectMin();
            p1 = ImGui::GetItemRectMax();
            stepPoint = {(p0.x + p1.x) * .5f, (p0.y + p1.y) * .5f};
            ImGui::SliderInt("Particle count", &particleCount, 1, 65536);
            ImGui::SliderFloat("Gravity", &gravity, 0, 10);
        }
        if (ImGui::Button("Reset parameters"))
            Reset();
        a = ImGui::GetItemRectMin();
        b = ImGui::GetItemRectMax();
        resetPoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
        if (stage >= 13)
            navigation.Hint();
        ImGui::PopItemWidth();
        ImGui::End();
        if (test && (frame == 12 || frame == 32) && environmentIntensity < 3)
            throw std::runtime_error("Environment slider input failed");
        if (test && frame == 22 && std::abs(environmentIntensity - 1) > 1e-5f)
            throw std::runtime_error("UI Reset failed");
    }
};
} // namespace tutorial
