#pragma once
#include "Application.h"
#include "CameraNavigation.h"
#include "RaytracingData.h"
#include <imgui.h>
namespace tutorial
{
struct RaytracingPanel
{
    CameraNavigation navigation;
    int scene = 0, camera = 0, preset = 1, view = 0, pathFrame = 0;
    bool visible = true, direct = true, ibl = true, shadows = true, reflections = true, bloom = true,
         pathEnabled = false, pathPlaying = false;
    float bias = .001f, exposure = 1, intensity = .8f, yaw = 0, threshold = 1, strength = .15f, occluder = 0;
    bool captureGpu = false, test = false;
    uint32_t frame = 0;
    ImVec2 editPoint{}, resetPoint{};
    std::filesystem::path screenshot;
    inline static constexpr const char *ViewNames[] = {"final",      "base",   "normal",     "depth",
                                                       "direct",     "shadow", "reflection", "extract",
                                                       "horizontal", "bloom"};
    void Reset(const Workshop &w)
    {
        navigation.Reset();
        scene = 0;
        preset = 1;
        view = 0;
        camera = 0;
        for (size_t i = 0; i < w.cameras.size(); ++i)
            if (w.cameras[i].name == "Hero")
                camera = int(i);
        direct = ibl = shadows = reflections = bloom = true;
        bias = .001f;
        exposure = w.exposure;
        intensity = w.environmentIntensity;
        yaw = w.environmentYaw;
        threshold = 1;
        strength = .15f;
        occluder = 0;
        pathEnabled = pathPlaying = false;
        pathFrame = 0;
    }
    void Initialize(const Workshop &w, const Options &o)
    {
        navigation.test = o.testCamera;
        Reset(w);
        visible = !o.hideUi;
        captureGpu = o.captureGpu;
        screenshot = o.screenshot;
        if (o.preset == L"Full")
            throw std::runtime_error(
                "Raytracing supports Opaque / OpaqueMask; Full (BLEND) is not supported");
        if (o.forward || o.benchmark)
            throw std::runtime_error("Raytracing does not support --forward / --benchmark");
        scene = o.workshopScene == L"Stress" ? 1 : 0;
        preset = o.preset == L"Opaque" ? 0 : 1;
        auto name =
            o.camera.empty() ? (scene ? "StressHero" : "Hero") : std::filesystem::path(o.camera).string();
        bool found = false;
        for (size_t i = 0; i < w.cameras.size(); ++i)
            if (w.cameras[i].name == name)
            {
                camera = int(i);
                found = true;
            }
        if (!found)
            throw std::runtime_error("Unknown Workshop camera");
        if (!o.shading.empty())
        {
            auto nameView = std::filesystem::path(o.shading).string();
            bool valid = false;
            for (int i = 0; i < 10; ++i)
                if (nameView == ViewNames[i])
                {
                    view = i;
                    valid = true;
                }
            if (!valid || (RT_TECHNIQUE == 1 && view > 1) || (RT_TECHNIQUE < 3 && view == 6))
                throw std::runtime_error("Unsupported raytracing view");
        }
        shadows = !o.disableShadows;
        direct = !o.disableDirect;
        ibl = !o.disableIbl;
        bloom = !o.disableBloom;
        reflections = !o.disableReflections;
        occluder = o.occluderOffset;
        threshold = o.bloomThreshold;
        strength = o.bloomStrength;
        pathEnabled = o.cameraPath;
        pathFrame = int(o.pathFrame);
        pathPlaying = pathEnabled && !o.frames;
        test = o.testUi;
        if (test && (!visible || o.frames < 36))
            throw std::runtime_error("--test-ui requires UI and at least 36 frames");
    }
    void QueueTestInput()
    {
        ++frame;
        if (!test)
            return;
        auto &io = ImGui::GetIO();
        io.AddFocusEvent(true);
        if (frame == 8 || frame == 18)
        {
            auto p = frame == 8 ? editPoint : resetPoint;
            io.AddMousePosEvent(p.x, p.y);
            io.AddMouseButtonEvent(0, true);
        }
        if (frame == 9 || frame == 19)
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
    void Draw(const Workshop &w, const char *api)
    {
        if (!visible)
            return;
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({330, 600}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Workshop Raytracing");
        ImGui::Text("%s / Advanced %02d", api, RT_TECHNIQUE);
        const char *scenes[] = {"Standard", "Stress"};
        if (ImGui::Combo("Scene", &scene, scenes, 2))
            for (size_t i = 0; i < w.cameras.size(); ++i)
                if (w.cameras[i].name == (scene ? "StressHero" : "Hero"))
                {
                    camera = int(i);
                    navigation.Reset();
                }
        const char *presets[] = {"Opaque", "OpaqueMask"};
        ImGui::Combo("Preset", &preset, presets, 2);
        if (ImGui::BeginCombo("Camera", w.cameras[camera].name.c_str()))
        {
            for (size_t i = 0; i < w.cameras.size(); ++i)
                if (ImGui::Selectable(w.cameras[i].name.c_str(), camera == int(i)))
                {
                    camera = int(i);
                    navigation.Reset();
                }
            ImGui::EndCombo();
        }
        if (ImGui::Checkbox("Camera path", &pathEnabled))
            navigation.Reset();
        ImGui::SameLine();
        ImGui::Checkbox("Play", &pathPlaying);
        ImGui::SliderInt("Path frame", &pathFrame, 0, 1200);
        if (ImGui::BeginCombo("View", ViewNames[view]))
        {
            for (int i = 0; i < 10; ++i)
            {
                if ((RT_TECHNIQUE == 1 && i > 1) || (RT_TECHNIQUE < 3 && i == 6))
                    continue;
                if (ImGui::Selectable(ViewNames[i], view == i))
                    view = i;
            }
            ImGui::EndCombo();
        }
        ImGui::SliderFloat("Occluder X", &occluder, -1.5f, 1.5f);
        auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        editPoint = {a.x + 175, (a.y + b.y) * .5f};
        if (RT_TECHNIQUE >= 2)
        {
            ImGui::Checkbox("Direct", &direct);
            ImGui::SameLine();
            ImGui::Checkbox("IBL", &ibl);
            ImGui::Checkbox("Shadows", &shadows);
            ImGui::SliderFloat("Ray offset (m)", &bias, .00001f, .01f, "%.5f");
            ImGui::SliderFloat("Environment", &intensity, 0, 4);
            ImGui::SliderFloat("Yaw", &yaw, -180, 180);
            ImGui::SliderFloat("Exposure", &exposure, .1f, 4);
            ImGui::Checkbox("Bloom", &bloom);
            ImGui::SliderFloat("Threshold", &threshold, 0, 10);
            ImGui::SliderFloat("Strength", &strength, 0, 2);
        }
        if (RT_TECHNIQUE == 3)
            ImGui::Checkbox("Reflections", &reflections);
        if (ImGui::Button("Reset"))
            Reset(w);
        a = ImGui::GetItemRectMin();
        b = ImGui::GetItemRectMax();
        resetPoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
        ImGui::Text("Opaque + MASK / explicit texture LOD 0");
        navigation.Hint();
        ImGui::End();
    }
};
} // namespace tutorial
