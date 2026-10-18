#pragma once
#include "CameraNavigation.h"
#include "DeferredData.h"
#ifdef LIGHT_CULLING
#include "LightGridData.h"
#endif
#include <imgui.h>
namespace tutorial
{
struct DeferredPanel
{
#ifdef INLINE_SAMPLE
    bool sunQuery=true, reflections=INLINE_SAMPLE==27;
    uint32_t shadowPointLights=8;
    int inlineView=0;
    float inlineBias=.001f;
#endif
    CameraNavigation navigation;
    int scene = 0, camera = 0, preset = 1, view = 0, pathFrame = 0;
#ifdef LIGHT_CULLING
    LightGridSettings grid;
    LightGridStatistics gridStatistics;
#endif
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
#ifdef INLINE_SAMPLE
        sunQuery=true;reflections=INLINE_SAMPLE==27;shadowPointLights=8;inlineView=0;inlineBias=.001f;
#endif
        navigation.Reset();
#ifdef LIGHT_CULLING
        grid = {};
        gridStatistics = {};
#endif
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
        navigation.test = o.testCamera;
        Reset(w);
#ifdef INLINE_SAMPLE
        sunQuery=o.sunShadow==L"rayquery";reflections=INLINE_SAMPLE==27 && !o.disableReflections;
        shadowPointLights=o.shadowPointLights;
        if(o.shading==L"reflection") {
#if INLINE_SAMPLE != 27
            throw std::runtime_error("Reflection diagnostic requires basic27");
#else
            inlineView=1;
#endif
        }
        if(o.shading==L"ray-count")inlineView=2;
#if INLINE_SAMPLE == 17
        if(o.lightCulling==L"cluster")throw std::runtime_error("Inline tiled sample: --light-culling all / tile");
#endif
#endif
#ifdef LIGHT_CULLING
        grid.perRoom = int(o.lightsPerRoom);
        grid.capacity = int(o.listCapacity);
        grid.radius = o.lightRadius;
        grid.intensity = o.lightIntensity;
        grid.animate = o.animateLights;
        if (!o.lightCulling.empty())
            grid.mode = o.lightCulling == L"all" ? 0 : o.lightCulling == L"tile" ? 1 : 2;
        grid.view = o.lightView == L"count"      ? 1
                    : o.lightView == L"slice"    ? 2
                    : o.lightView == L"overflow" ? 3
                                                 : 0;
#endif
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
                    {
                        camera = int(i);
                        navigation.Reset();
                    }
                    found = true;
                }
            if (!found)
                throw std::runtime_error("Unknown Workshop camera");
        }
        const std::array<std::wstring, 17> names = {
            L"final",     L"base",     L"ao",      L"normal-packed", L"normal", L"metallic",
            L"roughness", L"emissive", L"depth",   L"shadow-depth",  L"direct", L"diffuse",
            L"specular",  L"shadow",   L"extract", L"horizontal",    L"bloom"};
        if (!o.shading.empty()
#ifdef INLINE_SAMPLE
            && o.shading!=L"reflection" && o.shading!=L"ray-count"
#endif
            )
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
#ifdef INLINE_SAMPLE
            inlineView=0;
#endif
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
                {
                    camera = int(i);
                    navigation.Reset();
                }
        const char *presets[] = {"Opaque", "OpaqueMask", "Full"};
        ImGui::Combo("Preset", &preset, presets,
#ifdef INLINE_SAMPLE
                     2
#else
                     3
#endif
        );
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
#ifndef INLINE_SAMPLE
        ImGui::Checkbox("Forward diagnostic", &forward);
#endif
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
#ifdef INLINE_SAMPLE
        ImGui::BeginDisabled(sunQuery);
        ImGui::Checkbox("3x3 PCF", &pcf);
        ImGui::SliderFloat("Map bias", &bias, 0, .01f, "%.4f");
        ImGui::EndDisabled();
#else
        ImGui::Checkbox("3x3 PCF", &pcf);
        ImGui::SliderFloat("Bias", &bias, 0, .01f, "%.4f");
#endif
#ifdef INLINE_SAMPLE
        if(test && (frame==3 || frame==4))
            ImGui::SetNextItemOpen(frame==3,ImGuiCond_Always);
        if(ImGui::CollapsingHeader("Inline Raytracing")) {
            ImGui::PushItemWidth(150);
        ImGui::SliderFloat("Ray bias (m)",&inlineBias,.0001f,.01f,"%.4f");
        const char* methods[]={"Shadow Map","RayQuery"};int method=sunQuery?1:0;
        if(ImGui::Combo("Sun shadow",&method,methods,2))sunQuery=method==1;
#if INLINE_SAMPLE == 27
        ImGui::Checkbox("Reflections",&reflections);
#endif
#if INLINE_SAMPLE == 17
        int count=int(std::min(shadowPointLights,time.pointLights));
        if(ImGui::SliderInt("Point shadows",&count,0,int(time.pointLights)))shadowPointLights=uint32_t(count);
        if(ImGui::Button("Shadow all point lights"))shadowPointLights=UINT32_MAX;
#endif
#if INLINE_SAMPLE == 27
        const char* diagnostics[]={"Lighting","Incoming reflection","Shadow ray count"};
        if(ImGui::Combo("Inline diagnostic",&inlineView,diagnostics,3))view=0;
#else
        const char* diagnostics[]={"Lighting","Shadow ray count"};int diagnostic=inlineView==2?1:0;
        if(ImGui::Combo("Inline diagnostic",&diagnostic,diagnostics,2)){inlineView=diagnostic?2:0;view=0;}
#endif
            ImGui::PopItemWidth();
        }
#endif

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
        for (int i = 0; i < GpuPassCount; ++i)
            ImGui::Text("%-12s %.3f ms", GpuPassNames[i], time.milliseconds[i]);
#ifdef LIGHT_CULLING
        ImGui::SeparatorText("Many point lights");
        const char *modes[] = {"All lights", "Tiled", "Clustered"};
        ImGui::Combo("Culling", &grid.mode, modes, 3);
        ImGui::SliderInt("Lights / room", &grid.perRoom, 0, 512);
        ImGui::SliderFloat("Light radius", &grid.radius, .25f, 6);
        ImGui::SliderFloat("Point intensity", &grid.intensity, 0, 6);
        ImGui::Checkbox("Animate lights", &grid.animate);
        ImGui::SliderInt("List capacity", &grid.capacity, 1, 256);
        const char *lightViews[] = {"Lighting", "Candidates", "Z slice", "Overflow"};
        ImGui::Combo("Light view", &grid.view, lightViews, 4);
        ImGui::Text("Lights: %u, active cells: %u", time.pointLights, gridStatistics.active);
        ImGui::Text("Candidates avg %.1f / max %u", gridStatistics.average, gridStatistics.maximum);
        ImGui::Text("Overflow: %u (all-light fallback)", gridStatistics.overflow);
#endif
        navigation.Hint();
        ImGui::End();
    }
};
} // namespace tutorial
