#pragma once
#include "Application.h"
#include "LightingControls.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <stdexcept>

namespace tutorial
{
// Shared UI widgets only; each API owns its renderer, buffers, descriptors and synchronization.
struct ImGuiPanel
{
    LightingControls controls;
    uint32_t frame = 0;
    bool test = false;
    std::filesystem::path screenshot;
    ImVec2 intensityPoint{}, resetPoint{};

    uint32_t SceneLeft(uint32_t width) const
    {
        return std::min(350u, width / 2);
    }

    void Initialize(const GltfModel &model, const Options &options, uint32_t mode)
    {
        controls.Reset(model);
        controls.mode = static_cast<int>(mode);
        controls.rotate = options.rotateLight;
        test = options.testUi;
        screenshot = options.screenshot;
        if (test && (options.frames < 36 || options.rotateLight))
            throw std::runtime_error("--test-ui requires --frames >= 36 and a stationary light");
    }

    // Called after Win32_NewFrame and before NewFrame. Exercise real widgets, not their backing variables.
    void QueueTestInput()
    {
        ++frame;
        if (!test)
            return;
        auto &io = ImGui::GetIO();
        io.AddFocusEvent(true);
        const bool intensityClick = frame == 8 || frame == 28;
        const bool resetClick = frame == 18;
        if (intensityClick || resetClick)
        {
            const auto p = resetClick ? resetPoint : intensityPoint;
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
        const wchar_t *suffix = frame == 6    ? L"-baseline"
                                : frame == 14 ? L"-edited"
                                : frame == 24 ? L"-reset"
                                              : nullptr;
        return suffix ? screenshot.parent_path() / (screenshot.stem().wstring() + suffix + L".png")
                      : std::filesystem::path{};
    }

    void Draw(uint32_t width, uint32_t height, const GltfModel &model, const char *api)
    {
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({float(SceneLeft(width)) - 20, float(height) - 20}, ImGuiCond_Always);
        ImGui::Begin("PBR controls", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("%s / Sample 09", api);
        ImGui::TextUnformatted("UI -> CPU data -> GPU buffer -> draw");
        ImGui::SeparatorText("Directional light (linear RGB)");
        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat("Intensity", &controls.intensity, 0, 12, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        intensityPoint = {min.x + 160 * 0.85f, (min.y + max.y) * 0.5f};
        ImGui::ColorEdit3("Color", controls.lightColor, ImGuiColorEditFlags_Float);
        ImGui::SliderFloat("Yaw", &controls.yaw, -180, 180, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Elevation", &controls.elevation, -89, 89, "%.1f deg",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox("Rotate light", &controls.rotate);
        ImGui::SeparatorText("Environment and exposure");
        ImGui::SliderFloat("Ambient", &controls.ambient, 0, 0.2f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Exposure", &controls.exposure, 0.1f, 4, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SeparatorText("PBR material factors");
        ImGui::SliderFloat("Metallic", &controls.metallic, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Roughness", &controls.roughness, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Normal scale", &controls.normal, 0, 2, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("AO strength", &controls.ao, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Emissive scale", &controls.emissive, 0, 4, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        const char *modes[] = {"PBR",       "Albedo", "World normal", "Metallic",
                               "Roughness", "AO",     "Emissive",     "PBR without normal map"};
        ImGui::Combo("View", &controls.mode, modes, IM_ARRAYSIZE(modes));
        if (ImGui::Button("Reset parameters"))
            controls.Reset(model);
        const auto rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
        resetPoint = {(rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f};
        ImGui::TextUnformatted("LightingConstants: 320 bytes / frame");
        ImGui::TextUnformatted("One frame in flight; depth for 3D only.");
        ImGui::End();
        if (test && (frame == 12 || frame == 32))
        {
            if (controls.intensity < 9 || !ImGui::GetIO().WantCaptureMouse)
                throw std::runtime_error("ImGui slider input test failed");
            Trace("ImGui slider edited light intensity: " + std::to_string(controls.intensity));
        }
        if (test && frame == 22)
        {
            if (std::abs(controls.intensity - 3.0f) > 0.001f)
                throw std::runtime_error("ImGui reset button input test failed");
            Trace("ImGui reset button restored light intensity: 3.000000");
        }
    }
};
} // namespace tutorial
