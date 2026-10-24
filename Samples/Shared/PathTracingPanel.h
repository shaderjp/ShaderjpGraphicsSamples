#pragma once
#include "PathTracingData.h"
#include "RaytracingPanel.h"
namespace tutorial
{
struct PathTracingPanel : RaytracingPanel
{
    int bounces = 8, sampling = 1;
    bool paused = false, directOnly = false, resetRequested = true;
    float roughness = -1, emissionScale = 1;
    uint32_t seed = 1, targetSpp = 0, samples = 0;
    ImVec2 exposurePoint{}, pausePoint{};
    mutable uint32_t capturedStages = 0;
    void QueueTestInput()
    {
        // Synthetic UI tests submit press/release on separate frames. Consume all
        // queued events in that frame so OS focus events cannot delay a click.
        if (test) ImGui::GetIO().ConfigInputTrickleEventQueue = false;
        RaytracingPanel::QueueTestInput();
        if (test && (frame == 28 || frame == 29)) {
            auto &io = ImGui::GetIO(); io.AddMousePosEvent(exposurePoint.x, exposurePoint.y);
            io.AddMouseButtonEvent(0, frame == 28);
        }
        if (test && (frame == 38 || frame == 39 || frame == 44 || frame == 45)) {
            auto &io = ImGui::GetIO(); io.AddMousePosEvent(pausePoint.x, pausePoint.y);
            io.AddMouseButtonEvent(0, frame == 38 || frame == 44);
        }
    }
    std::filesystem::path TestCapture() const
    {
        if (!test || screenshot.empty()) return {};
        if (targetSpp && samples < targetSpp) return {};
        struct Stage { uint32_t first, last; const wchar_t *suffix; };
        const Stage stages[] = {{7, 7, L"-baseline"}, {14, 17, L"-edited"}, {25, 27, L"-reset"},
            {34, 37, L"-exposure"}, {41, 42, L"-paused"}, {43, 43, L"-paused-later"}, {50, 51, L"-resumed"}};
        for (uint32_t i = 0; i < std::size(stages); ++i)
            if (frame >= stages[i].first && frame <= stages[i].last && !(capturedStages & (1u << i))) {
                capturedStages |= 1u << i;
                return screenshot.parent_path() / (screenshot.stem().wstring() + stages[i].suffix + L".png");
            }
        return {};
    }
    void Initialize(const Workshop &w, const Options &o)
    {
        RaytracingPanel::Initialize(w, o);
        scene = o.ptScene == L"room" ? 0 : o.workshopScene == L"Stress" ? 2 : 1;
        if (!o.rayTestScene.empty()) scene = 0;
        camera = 0;
        std::string name = !o.camera.empty() ? std::filesystem::path(o.camera).string() :
            scene == 0 ? w.cameras.front().name : scene == 2 ? "StressHero" : "Hero";
        for (size_t i = 0; i < w.cameras.size(); ++i) if (w.cameras[i].name == name) camera = int(i);
        bounces = int(o.ptMaxBounces); sampling = o.ptSampling == L"mis"; seed = o.ptSeed; targetSpp = o.ptTargetSpp;
        bloom = false; view = 0; pathEnabled = pathPlaying = false; paused = o.pauseParticles;
        intensity = scene ? .8f : 0.f; exposure = 1;
        if (o.disableIbl) intensity = 0;
        if (targetSpp && o.frames && o.frames < targetSpp && !o.testWindow && !o.testUi)
            throw std::runtime_error("--frames must be >= --pt-target-spp");
    }
    void Draw(const Workshop &w, const char *api)
    {
        if (!visible) return;
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({340, 640}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Path tracing / Advanced 18");
        ImGui::Text("%s / %u spp", api, samples);
        if (ImGui::BeginCombo("Scene", w.scenes.at(scene).name.c_str())) {
            for (int i = 0; i < int(w.scenes.size()); ++i) if (ImGui::Selectable(w.scenes[i].name.c_str(), i == scene)) {
                scene = i; camera = 0; navigation.Reset(); intensity = i ? .8f : 0;
                for (int c = 0; c < int(w.cameras.size()); ++c)
                    if (w.cameras[c].name == (i == 0 ? "Room" : i == 2 ? "StressHero" : "Hero")) camera = c;
            }
            ImGui::EndCombo();
        }
        const char *presets[] = {"Opaque", "Opaque + MASK"}; ImGui::Combo("Preset", &preset, presets, 2);
        if (ImGui::BeginCombo("Camera", w.cameras.at(camera).name.c_str())) {
            for (int i = 0; i < int(w.cameras.size()); ++i) if (ImGui::Selectable(w.cameras[i].name.c_str(), i == camera)) { camera = i; navigation.Reset(); }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Direct lighting only", &directOnly);
        const char *modes[] = {"BSDF (+ delta lights)", "NEE + MIS"}; ImGui::Combo("Sampling", &sampling, modes, 2);
        ImGui::SliderInt("Max bounces", &bounces, 1, 32);
        ImGui::SliderFloat("Roughness", &roughness, -1, 1, "%.2f");
        ImGui::TextUnformatted("Negative: use material roughness");
        ImGui::SliderFloat("Occluder X", &occluder, -1.5f, 1.5f);
        auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); editPoint = {a.x + 175, (a.y + b.y) * .5f};
        ImGui::SliderFloat("Emitter scale", &emissionScale, 0, 3);
        ImGui::SliderFloat("Environment", &intensity, 0, 3);
        ImGui::SliderFloat("Environment yaw", &yaw, -180, 180);
        ImGui::SliderFloat("Exposure", &exposure, .1f, 4);
        a = ImGui::GetItemRectMin(); b = ImGui::GetItemRectMax(); exposurePoint = {a.x + 180, (a.y + b.y) * .5f};
        ImGui::Checkbox("Pause accumulation", &paused);
        a = ImGui::GetItemRectMin(); b = ImGui::GetItemRectMax(); pausePoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
        if (ImGui::Button("Reset accumulation")) resetRequested = true;
        if (ImGui::Button("Reset")) {
            navigation.Reset(); directOnly = false; bounces = 8; sampling = 1; roughness = -1;
            occluder = 0; emissionScale = 1; exposure = 1; yaw = 0; intensity = scene ? .8f : 0;
            paused = false; resetRequested = true;
        }
        a = ImGui::GetItemRectMin(); b = ImGui::GetItemRectMax(); resetPoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
        ImGui::TextUnformatted("1 sample / frame, full resolution"); navigation.Hint(); ImGui::End();
    }
    RayConstants Constants(const Workshop &w, uint32_t width, uint32_t height, uint32_t sample, uint32_t lightCount) const
    {
        auto c = MakeRayConstants(w, scene, camera, -1, width, height, 0, true, true, direct, true,
            intensity, yaw, bias, navigation.Position(), navigation.Target(), navigation.Projection());
        c.controls = {uint32_t(w.scenes[scene].lights.size()), lightCount, sample, uint32_t(directOnly ? 1 : bounces)};
        c.lighting = {uint32_t(direct), 1, seed, uint32_t(sampling)};
        c.environment.z = roughness; c.environment.w = emissionScale;
        return c;
    }
};
} // namespace tutorial
