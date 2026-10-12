#pragma once
#include "LightingScene.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
namespace tutorial
{
struct LightingLabPanel
{
    int stage = 10, environmentIndex = 0, view = 0;
    float environmentIntensity = 1, yaw = 0, directIntensity = 3, exposure = 1, bias = .001f, occluder = 0;
    bool shadows = true, pcf = true, visible = true, test = false;
    uint32_t frame = 0;
    std::filesystem::path screenshot;
    ImVec2 intensityPoint{}, resetPoint{};
    float initialIntensity = 3, initialExposure = 1;
    void Reset()
    {
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
        stage = technique;
        initialIntensity = s.intensity;
        initialExposure = s.exposure;
        visible = !o.hideUi;
        Reset();
        test = o.testUi;
        screenshot = o.screenshot;
        occluder = o.occluderOffset;
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
        if (!test)
            return;
        auto &io = ImGui::GetIO();
        io.AddFocusEvent(true);
        if (frame == 8 || frame == 28 || frame == 18)
        {
            auto p = frame == 18 ? resetPoint : intensityPoint;
            io.AddMousePosEvent(p.x, p.y);
            io.AddMouseButtonEvent(0, true);
        }
        if (frame == 9 || frame == 19 || frame == 29)
            io.AddMouseButtonEvent(0, false);
    }
    std::filesystem::path TestCapture() const
    {
        if (!test || screenshot.empty())
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
        if (stage == 13)
        {
            ImGui::Checkbox("Shadow enabled", &shadows);
            ImGui::Checkbox("3x3 PCF", &pcf);
            ImGui::SliderFloat("Receiver bias", &bias, 0, .01f, "%.4f");
            ImGui::SliderFloat("Occluder X", &occluder, -1.5f, 1.5f);
        }
        if (ImGui::Button("Reset parameters"))
            Reset();
        a = ImGui::GetItemRectMin();
        b = ImGui::GetItemRectMax();
        resetPoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
        ImGui::PopItemWidth();
        ImGui::End();
        if (test && (frame == 12 || frame == 32) && environmentIntensity < 3)
            throw std::runtime_error("Environment slider input failed");
        if (test && frame == 22 && std::abs(environmentIntensity - 1) > 1e-5f)
            throw std::runtime_error("UI Reset failed");
    }
};
} // namespace tutorial
