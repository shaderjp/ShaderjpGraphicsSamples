#pragma once
#include "DeferredData.h"
#include <imgui.h>
namespace tutorial
{
struct DeferredPanel
{
    int scene = 0, camera = 0, preset = 1, view = 0, pathFrame = 0;
    bool visible = true, forward = false, direct = true, ibl = true, shadows = true, pcf = true, bloom = true,
         pathEnabled = false, pathPlaying = false;
    float bias = .001f, exposure = 1, intensity = .8f, yaw = 0, threshold = 1, strength = .15f, occluder = 0;
    bool captureGpu = false;
    uint32_t frame = 0;
    bool test = false;
    ImVec2 intensityPoint{}, resetPoint{};
    std::filesystem::path screenshot;
    void Reset(const Workshop &w)
    {
        scene = 0;
        preset = 1;
        view = 0;
        camera = 0;
        for (size_t i = 0; i < w.cameras.size(); ++i)
            if (w.cameras[i].name == "Hero")
                camera = int(i);
        forward = false;
        direct = ibl = shadows = pcf = bloom = true;
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
        Reset(w);
        visible = !o.hideUi;
        captureGpu = o.captureGpu;
        screenshot = o.screenshot;
        occluder = o.occluderOffset;
        threshold = o.bloomThreshold;
        strength = o.bloomStrength;
        scene = o.workshopScene == L"Stress" ? 1 : 0;
        forward = o.forward;
        preset = o.preset == L"Full" ? 2 : o.preset == L"Opaque" ? 0 : 1;
        if (!o.camera.empty())
        {
            bool found = false;
            for (size_t i = 0; i < w.cameras.size(); ++i)
                if (w.cameras[i].name == std::filesystem::path(o.camera).string())
                {
                    camera = int(i);
                    found = true;
                }
            if (!found)
                throw std::runtime_error("Unknown Workshop camera");
        }
        const std::array<std::wstring, 17> names = {
            L"final",     L"base",     L"ao",      L"normal-packed", L"normal", L"metallic",
            L"roughness", L"emissive", L"depth",   L"shadow-depth",  L"direct", L"diffuse",
            L"specular",  L"shadow",   L"extract", L"horizontal",    L"bloom"};
        if (!o.shading.empty())
        {
            auto it = std::find(names.begin(), names.end(), o.shading);
            if (it == names.end())
                throw std::runtime_error("Unknown Deferred view");
            view = int(it - names.begin());
        }
        pathEnabled = o.cameraPath;
        pathFrame = int(o.pathFrame);
        pathPlaying = o.cameraPath && !o.frames;
        shadows = !o.disableShadows;
        pcf = !o.hardShadows;
        bloom = !o.disableBloom;
        ibl = !o.disableIbl;
        direct = !o.disableDirect;
        test = o.testUi;
        if (!o.testImage.empty() && o.testImage != L"black" && o.testImage != L"constant" &&
            o.testImage != L"impulse")
            throw std::runtime_error("--test-image: black / constant / impulse");
        if (forward && view >= 1 && view <= 9)
            throw std::runtime_error("G-buffer views require Deferred mode");
        if (test && (!visible || o.frames < 36))
            throw std::runtime_error("--test-ui requires visible UI and 36 frames");
        if (o.benchmark)
        {
            preset = 1;
            visible = false;
            exposure = 1;
            intensity = .8f;
            threshold = 1;
            strength = .15f;
            occluder = 0;
            shadows = pcf = bloom = direct = ibl = true;
            if (o.frames != 1260 || o.testWindow || test)
                throw std::runtime_error("Benchmark requires 1260 frames without resize or UI tests");
        }
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
            auto p = frame == 8 ? intensityPoint : resetPoint;
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
        const wchar_t *suffix = frame == 6    ? L"-baseline"
                                : frame == 14 ? L"-edited"
                                : frame == 24 ? L"-reset"
                                              : nullptr;
        return suffix ? screenshot.parent_path() / (screenshot.stem().wstring() + suffix + L".png")
                      : std::filesystem::path{};
    }
    bool Includes(AlphaMode mode) const
    {
        return mode == AlphaMode::Opaque || (mode == AlphaMode::Mask && preset >= 1) ||
               (mode == AlphaMode::Blend && preset == 2);
    }
    void Draw(const Workshop &w, const DeferredTiming &time, const char *api)
    {
        if (!visible)
            return;
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({330, 650}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Workshop Deferred");
        ImGui::Text("%s / %s", api, DEFERRED_COMPUTE ? "Compute" : "Pixel");
        const char *scenes[] = {"Standard", "Stress"};
        if (ImGui::Combo("Scene", &scene, scenes, 2))
            for (size_t i = 0; i < w.cameras.size(); ++i)
                if (w.cameras[i].name == (scene ? "StressHero" : "Hero"))
                    camera = int(i);
        const char *presets[] = {"Opaque", "OpaqueMask", "Full"};
        ImGui::Combo("Preset", &preset, presets, 3);
        if (ImGui::BeginCombo("Camera", w.cameras[camera].name.c_str()))
        {
            for (size_t i = 0; i < w.cameras.size(); ++i)
                if (ImGui::Selectable(w.cameras[i].name.c_str(), camera == int(i)))
                    camera = int(i);
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Camera path", &pathEnabled);
        ImGui::SameLine();
        ImGui::Checkbox("Play", &pathPlaying);
        ImGui::SliderInt("Path frame", &pathFrame, 0, 1200);
        ImGui::Checkbox("Forward diagnostic", &forward);
        if (forward && view >= 1 && view <= 9)
            view = 0;
        const char *views[] = {"Final",
                               "Base Color",
                               "AO",
                               "Packed normal",
                               "World normal",
                               "Metallic",
                               "Roughness",
                               "Emissive HDR",
                               "Depth",
                               "Shadow Map",
                               "Direct",
                               "Diffuse IBL",
                               "Specular IBL",
                               "Shadow visibility",
                               "Bloom extract",
                               "Horizontal blur",
                               "Bloom"};
        ImGui::Combo("View", &view, views, 17);
        ImGui::Checkbox("Direct", &direct);
        ImGui::SameLine();
        ImGui::Checkbox("IBL", &ibl);
        ImGui::Checkbox("Shadow", &shadows);
        ImGui::SameLine();
        ImGui::Checkbox("3x3 PCF", &pcf);
        ImGui::SliderFloat("Bias", &bias, 0, .01f, "%.4f");
        ImGui::SliderFloat("Occluder X", &occluder, -1.5f, 1.5f);
        ImGui::SliderFloat("Environment", &intensity, 0, 4);
        auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        intensityPoint = {a.x + 150, (a.y + b.y) * .5f};
        ImGui::SliderFloat("Yaw", &yaw, -180, 180);
        ImGui::SliderFloat("Exposure", &exposure, .1f, 4);
        ImGui::Checkbox("Bloom", &bloom);
        ImGui::SliderFloat("Threshold", &threshold, 0, 10);
        ImGui::SliderFloat("Strength", &strength, 0, 2);
        if (ImGui::Button("Reset"))
            Reset(w);
        a = ImGui::GetItemRectMin();
        b = ImGui::GetItemRectMax();
        resetPoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
        ImGui::Text("G-buffer: 18 B/pixel");
        for (int i = 0; i < 10; ++i)
            ImGui::Text("%-12s %.3f ms", GpuPassNames[i], time.milliseconds[i]);
        ImGui::End();
    }
};
} // namespace tutorial
