#pragma once
#include "DeferredPanel.h"
#include "MeshTiming.h"
#include "MeshletData.h"
namespace tutorial
{
struct MeshPanel : DeferredPanel
{
    int renderPath = MESH_TECHNIQUE == 4 ? 1 : MESH_TECHNIQUE == 5 ? 3 : 2, culling = 2;
    bool meshletView = false;
    uint32_t candidateCount = 0, visibleCount = 0;
    void Reset(const Workshop &w)
    {
        DeferredPanel::Reset(w);
        preset = 1;
        forward = MESH_TECHNIQUE < 8;
        shadows = MESH_TECHNIQUE >= 7;
        direct = ibl = bloom = MESH_TECHNIQUE != 4;
        renderPath = MESH_TECHNIQUE == 4 ? 1 : MESH_TECHNIQUE == 5 ? 3 : 2;
        culling = 2;
        meshletView = false;
        grid.mode = MESH_TECHNIQUE == 9 ? 1 : 0;
        grid.perRoom = MESH_TECHNIQUE == 9 ? 256 : 0;
    }
    void Initialize(const Workshop &w, const Options &o)
    {
        if (o.preset == L"Full")
            throw std::runtime_error("Mesh Shader supports Opaque / OpaqueMask; Full (BLEND) is not supported");
        DeferredPanel::Initialize(w, o);
        forward = MESH_TECHNIQUE < 8;
        shadows = MESH_TECHNIQUE >= 7 && !o.disableShadows;
        if (MESH_TECHNIQUE == 4)
        {
            scene = 0;
            camera = 0;
            preset = 0;
            direct = ibl = bloom = false;
            shadows = false;
            view = 0;
        }
        grid.mode = MESH_TECHNIQUE == 9 ? grid.mode : 0;
        grid.perRoom = MESH_TECHNIQUE == 9 ? grid.perRoom : 0;
        const std::wstring paths[] = {L"vertex", L"mesh", L"task", L"indirect"};
        if (!o.meshRenderPath.empty())
        {
            auto it = std::find(std::begin(paths), std::end(paths), o.meshRenderPath);
            if (it == std::end(paths))
                throw std::runtime_error("--render-path: vertex / mesh / task / indirect");
            renderPath = int(it - std::begin(paths));
        }
        if ((MESH_TECHNIQUE == 4 && renderPath > 1) || (MESH_TECHNIQUE == 5 && renderPath == 2) ||
            (MESH_TECHNIQUE >= 6 && renderPath == 3))
            throw std::runtime_error("Unsupported render path for this Mesh Shader sample");
        const std::wstring culls[] = {L"off", L"frustum", L"frustum-cone"};
        if (!o.meshCulling.empty())
        {
            auto it = std::find(std::begin(culls), std::end(culls), o.meshCulling);
            if (it == std::end(culls))
                throw std::runtime_error("--meshlet-culling: off / frustum / frustum-cone");
            culling = int(it - std::begin(culls));
        }
        if (!o.meshView.empty() && o.meshView != L"off" && o.meshView != L"meshlets")
            throw std::runtime_error("--meshlet-view: off / meshlets");
        meshletView = o.meshView == L"meshlets";
        if (renderPath == 0 && meshletView)
            throw std::runtime_error("Meshlet colors require a Mesh Shader render path");
        if (MESH_TECHNIQUE == 9 && grid.mode == 2)
            throw std::runtime_error("Mesh Tiled Deferred supports --light-culling all / tile");
    }
    void Draw(const Workshop &w, const MeshTiming &time, const char *api)
    {
        if (!visible)
            return;
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({340, 650}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Mesh Shader Workshop");
#ifdef INLINE_SAMPLE
        ImGui::Text("%s / Advanced 17 / RayQuery", api);
#else
        ImGui::Text("%s / Advanced %02d", api, MESH_TECHNIQUE);
#endif
        const char *paths[] = {"Vertex reference", "Mesh direct", "AS / Task", "Compute + Indirect"};
        if (ImGui::BeginCombo("Render path", paths[renderPath]))
        {
            for (int i = 0; i < 4; ++i)
            {
                if ((MESH_TECHNIQUE == 4 && i > 1) || (MESH_TECHNIQUE == 5 && i == 2) ||
                    (MESH_TECHNIQUE >= 6 && i == 3))
                    continue;
                if (ImGui::Selectable(paths[i], i == renderPath))
                {
                    renderPath = i;
                    if (i == 0)
                        meshletView = false;
                }
            }
            ImGui::EndCombo();
        }
        if (MESH_TECHNIQUE != 4)
        {
            const char *culls[] = {"OFF", "Frustum", "Frustum + cone"};
            ImGui::Combo("Meshlet culling", &culling, culls, 3);
            ImGui::BeginDisabled(renderPath == 0);
            ImGui::Checkbox("Meshlet colors", &meshletView);
            ImGui::EndDisabled();
            ImGui::Text("Candidates %u / Visible %u", candidateCount, visibleCount);
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
        }
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
        ImGui::SliderFloat("Occluder X", &occluder, -1.5f, 1.5f);
        auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        intensityPoint = {a.x + 175, (a.y + b.y) * .5f};
        if (MESH_TECHNIQUE != 4)
        {
            ImGui::Checkbox("Direct", &direct);
            ImGui::SameLine();
            ImGui::Checkbox("IBL", &ibl);
            ImGui::SliderFloat("Environment", &intensity, 0, 4);
            ImGui::SliderFloat("Exposure", &exposure, .1f, 4);
            ImGui::Checkbox("Bloom", &bloom);
        }
        if (MESH_TECHNIQUE >= 7)
        {
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
        }
        if (MESH_TECHNIQUE >= 8)
        {
            const char *views[] = {"Final",        "Base",     "AO",      "Packed normal", "Normal", "Metallic",
                                   "Roughness",    "Emissive", "Depth",   "Shadow depth",  "Direct", "Diffuse IBL",
                                   "Specular IBL", "Shadow",   "Extract", "Horizontal",    "Bloom"};
            ImGui::Combo("View", &view, views, 17);
        }
        if (MESH_TECHNIQUE == 9)
        {
#ifdef INLINE_SAMPLE
            if(test && (frame==3 || frame==4))
                ImGui::SetNextItemOpen(frame==3,ImGuiCond_Always);
            if(ImGui::CollapsingHeader("Inline Raytracing")) {
            ImGui::PushItemWidth(150);
            ImGui::SliderFloat("Ray bias (m)",&inlineBias,.0001f,.01f,"%.4f");
            const char* methods[]={"Shadow Map","RayQuery"};int method=sunQuery?1:0;
            if(ImGui::Combo("Sun shadow",&method,methods,2))sunQuery=method==1;
            int count=int(std::min(shadowPointLights,time.pointLights));
            if(ImGui::SliderInt("Point shadows",&count,0,int(time.pointLights)))shadowPointLights=uint32_t(count);
            if(ImGui::Button("Shadow all point lights"))shadowPointLights=UINT32_MAX;
            const char* diagnostics[]={"Lighting","Shadow ray count"};int diagnostic=inlineView==2?1:0;
            if(ImGui::Combo("Inline diagnostic",&diagnostic,diagnostics,2)){inlineView=diagnostic?2:0;view=0;grid.view=0;}
                ImGui::PopItemWidth();
            }
#endif
            const char *modes[] = {"All lights", "Tiled"};
            ImGui::Combo("Light culling", &grid.mode, modes, 2);
            ImGui::SliderInt("Lights / room", &grid.perRoom, 0, 512);
            ImGui::SliderInt("List capacity", &grid.capacity, 1, 256);
            const char *views[] = {"Lighting", "Candidates", "Z slice", "Overflow"};
            ImGui::Combo("Light view", &grid.view, views, 4);
            ImGui::Text("Lights %u / candidates %.1f / overflow %u", time.pointLights, gridStatistics.average,
                        gridStatistics.overflow);
        }
        if (ImGui::Button("Reset"))
            Reset(w);
        a = ImGui::GetItemRectMin();
        b = ImGui::GetItemRectMax();
        resetPoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
        ImGui::Text("Geometry CPU %.3f ms", time.cpuMilliseconds);
        if (MESH_TECHNIQUE == 5)
            ImGui::Text("Compute culling %.3f ms", time.cullMilliseconds);
        for (int i = 0; i < GpuPassCount; ++i)
            ImGui::Text("%-12s %.3f ms", GpuPassNames[i], time.milliseconds[i]);
        navigation.Hint();
        ImGui::End();
    }
};
} // namespace tutorial
