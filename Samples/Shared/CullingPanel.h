#pragma once
#include "CullingData.h"
#include "DeferredPanel.h"
namespace tutorial
{
struct CullingPanel : DeferredPanel
{
    CullingState culling;
    float objectOffset = 0;
    int inspectObject = 0;
    ImVec2 offPoint{};
    bool Supported(CullMode mode) const
    {
        return mode == CullMode::Off || mode == CullMode::Frustum ||
               (CULL_SAMPLE == 32 && (mode == CullMode::Depth || mode == CullMode::Query)) ||
               (CULL_SAMPLE == 33 && mode == CullMode::Software) ||
               (CULL_SAMPLE == 34 && (mode == CullMode::Depth || mode == CullMode::HiZ));
    }
    void Reset(const Workshop &w)
    {
        DeferredPanel::Reset(w);
        forward = true;
        shadows = bloom = false;
        preset = 1;
        objectOffset = 0;
        inspectObject = 0;
        for (size_t i = 0; i < w.scenes.size(); ++i)
            if (w.scenes[i].name == "Standard")
                scene = int(i);
        for (size_t i = 0; i < w.scenes[scene].instances.size(); ++i)
            if (w.scenes[scene].instances[i].id.starts_with("object:"))
            {
                inspectObject = int(i);
                break;
            }
        culling = {};
        culling.mode = CULL_SAMPLE == 31   ? CullMode::Frustum
                       : CULL_SAMPLE == 32 ? CullMode::Query
                       : CULL_SAMPLE == 33 ? CullMode::Software
                                           : CullMode::HiZ;
    }
    void QueueTestInput()
    {
        DeferredPanel::QueueTestInput();
        if (!test)
            return;
        auto &io = ImGui::GetIO();
        if (frame == 28 || frame == 38)
        {
            auto p = frame == 28 ? offPoint : resetPoint;
            Trace("Culling UI click frame=" + std::to_string(frame) + " x=" + std::to_string(p.x) +
                  " y=" + std::to_string(p.y));
            io.AddMousePosEvent(p.x, p.y);
            io.AddMouseButtonEvent(0, true);
        }
        if (frame == 29 || frame == 39)
            io.AddMouseButtonEvent(0, false);
    }
    std::filesystem::path TestCapture() const
    {
        if (!test || screenshot.empty())
            return {};
        const wchar_t *suffix = frame == 34 ? L"-off" : frame == 44 ? L"-mode-reset" : nullptr;
        return suffix ? screenshot.parent_path() / (screenshot.stem().wstring() + suffix + L".png")
                      : DeferredPanel::TestCapture();
    }
    void Initialize(const Workshop &w, const Options &o)
    {
        DeferredPanel::Initialize(w, o);
        Reset(w);
        visible = !o.hideUi && !o.benchmark;
        captureGpu = o.captureGpu;
        screenshot = o.screenshot;
        test = o.testUi;
        pathEnabled = o.cameraPath || o.benchmark;
        pathFrame = int(o.pathFrame);
        occluder = o.occluderOffset;
        objectOffset = o.cullingObjectOffset;
        const auto &selected = o.cullingScene.empty() ? o.workshopScene : o.cullingScene;
        if (!selected.empty())
        {
            bool found = false;
            for (size_t i = 0; i < w.scenes.size(); ++i)
                if (w.scenes[i].name == std::filesystem::path(selected).string())
                {
                    scene = int(i);
                    found = true;
                }
            if (!found)
                throw std::runtime_error("Culling scene: Mini / Standard / Stress");
        }
        if (!o.camera.empty())
            for (size_t i = 0; i < w.cameras.size(); ++i)
                if (w.cameras[i].name == std::filesystem::path(o.camera).string())
                    camera = int(i);
        if (!o.culling.empty())
        {
            bool found = false;
            for (int i = 0; i < 6; ++i)
                if (std::filesystem::path(o.culling).string() == CullNames[i])
                {
                    culling.mode = CullMode(i);
                    found = true;
                }
            if (!found || !Supported(culling.mode))
                throw std::runtime_error("Culling mode is unsupported by this sample");
        }
        const wchar_t *views[] = {L"final", L"bounds", L"depth", L"hiz", L"ids"};
        bool found = o.cullingView.empty();
        for (int i = 0; i < 5; ++i)
            if (o.cullingView == views[i])
            {
                culling.diagnostic = i;
                found = true;
            }
        if (!found)
            throw std::runtime_error("--culling-view: final / bounds / depth / hiz / ids");
        culling.divisor = int(o.cullingDivisor);
        culling.mip = int(o.cullingMip);
        culling.freeze = o.cullingFreeze;
        if (culling.freeze && CULL_SAMPLE != 31)
            throw std::runtime_error("Frozen culling camera is a sample 31 diagnostic");
        if (culling.diagnostic == 3 && CULL_SAMPLE != 34)
            throw std::runtime_error("Hi-Z mip visualization requires sample 34");
        if (culling.divisor != 2 && culling.divisor != 4 && culling.divisor != 8)
            throw std::runtime_error("--culling-divisor: 2 / 4 / 8");
    }
    void Draw(const Workshop &w, const DeferredTiming &time, const char *api)
    {
        if (!visible)
            return;
        ImGui::SetNextWindowPos({12, 12}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({410, 710}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Traditional Culling");
        ImGui::Text("%s / Sample %d", api, CULL_SAMPLE);
        if (ImGui::BeginCombo("Scene", w.scenes[scene].name.c_str()))
        {
            for (size_t i = 0; i < w.scenes.size(); ++i)
                if (ImGui::Selectable(w.scenes[i].name.c_str(), scene == int(i)))
                {
                    scene = int(i);
                    culling.frozen = false;
                }
            ImGui::EndCombo();
        }
        ImGui::TextUnformatted("Culling");
        for (int i = 0; i < 6; ++i)
            if (Supported(CullMode(i)))
            {
                if (i != 0)
                    ImGui::SameLine();
                if (ImGui::RadioButton(CullNames[i], int(culling.mode) == i))
                    culling.mode = CullMode(i);
                if (i == 0)
                {
                    auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                    offPoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
                }
            }
        const char *views[] = {"Final", "Bounds / reason", "Depth", "Hi-Z mip", "Object IDs"};
        if (ImGui::BeginCombo("View", views[culling.diagnostic]))
        {
            for (int i = 0; i < 5; ++i)
                if ((i != 3 || CULL_SAMPLE == 34) && ImGui::Selectable(views[i], culling.diagnostic == i))
                    culling.diagnostic = i;
            ImGui::EndCombo();
        }
        if (CULL_SAMPLE == 33)
        {
            int v = culling.divisor == 2 ? 0 : culling.divisor == 4 ? 1 : 2;
            const char *d[] = {"1/2", "1/4", "1/8"};
            if (ImGui::Combo("CPU resolution", &v, d, 3))
                culling.divisor = 2 << v;
        }
        if (CULL_SAMPLE == 34)
            ImGui::SliderInt("Mip", &culling.mip, 0, 13);
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
        if (CULL_SAMPLE == 31)
            ImGui::Checkbox("Freeze culling camera", &culling.freeze);
        if (culling.freeze)
            ImGui::TextWrapped("Diagnostic: observer and culling cameras differ.");
        ImGui::SliderFloat("Door X", &occluder, 0, 2.5f);
        auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        intensityPoint = {a.x + 120, (a.y + b.y) * .5f};
        ImGui::SliderFloat("Object 0 X", &objectOffset, -4, 4);
        if (ImGui::Button("Reset"))
            Reset(w);
        a = ImGui::GetItemRectMin();
        b = ImGui::GetItemRectMax();
        resetPoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
        ImGui::Separator();
        ImGui::Text("Objects %u / frustum rejected %u", culling.total, culling.frustumRejected);
        ImGui::Text("Occluded %u / candidates %u", culling.occluded, culling.gpuObjects);
        ImGui::Text("CPU color Draws %u / GPU objects %u", culling.recorded, culling.gpuObjects);
        ImGui::Text("CPU cull %.3f / record %.3f ms", culling.cpuMs, culling.recordMs);
        const char *passes[] = {"Depth", "Query + resolve", "Hi-Z build", "Hi-Z test"};
        for (int i = 0; i < 4; ++i)
            ImGui::Text("%s %.3f ms", passes[i], culling.gpuMs[i]);
        ImGui::Text("GPU frame %.3f ms", time.milliseconds[9]);
        navigation.Hint();
        ImGui::End();
        if (culling.diagnostic == 1)
        {
            ImGui::SetNextWindowPos({440, 12}, ImGuiCond_Always);
            ImGui::Begin("Bounds legend", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("Green: inside / cyan: intersect / red: outside");
            ImGui::Text("Orange: occluded / blue: occluder");
            if (CULL_SAMPLE == 34 && !culling.bounds.empty())
            {
                inspectObject = std::clamp(inspectObject, 0, int(culling.bounds.size()) - 1);
                ImGui::SliderInt("Inspect object", &inspectObject, 0, int(culling.bounds.size()) - 1);
                const auto &objects = w.scenes[scene].instances;
                if (size_t(inspectObject) < objects.size())
                    ImGui::TextUnformatted(objects[inspectObject].id.c_str());
                ImGui::Text("White rectangle: all referenced Hi-Z cells");
            }
            ImGui::End();
        }
    }
    void Overlay(uint32_t width, uint32_t height)
    {
        if (culling.diagnostic != 1)
            return;
        auto *draw = ImGui::GetBackgroundDrawList();
        using namespace DirectX;
        auto drawEdges = [&](const std::array<XMVECTOR, 8> &corners, ImU32 color, float thickness)
        {
            ImVec2 points[8];
            bool safe[8];
            for (int k = 0; k < 8; ++k)
            {
                XMFLOAT4 p;
                XMStoreFloat4(&p, XMVector4Transform(corners[k], XMLoadFloat4x4(&culling.observer)));
                safe[k] = p.w > 1e-5f && p.z > 0;
                points[k] = {(p.x / p.w * .5f + .5f) * width, (.5f - p.y / p.w * .5f) * height};
            }
            for (int k = 0; k < 8; ++k)
                for (int axis = 0; axis < 3; ++axis)
                {
                    int end = k ^ (1 << axis);
                    if (k < end && safe[k] && safe[end])
                        draw->AddLine(points[k], points[end], color, thickness);
                }
        };
        for (size_t i = 0; i < culling.bounds.size(); ++i)
        {
            auto p = ProjectCullBounds(culling.bounds[i], culling.observer, width, height);
            if (!p.safe)
                continue;
            ImU32 color = culling.classification[i] == 0   ? IM_COL32(250, 90, 80, 170)
                          : !culling.visible[i]            ? IM_COL32(255, 175, 45, 190)
                          : culling.classification[i] == 1 ? IM_COL32(70, 230, 240, 190)
                                                           : IM_COL32(70, 230, 150, 170);
            if (culling.Occluder(i) && culling.classification[i] != 0)
                color = IM_COL32(100, 165, 255, 180);
            std::array<XMVECTOR, 8> corners;
            for (int k = 0; k < 8; ++k)
                corners[k] = CullCorner(culling.bounds[i], k);
            drawEdges(corners, color, 1);
            if (CULL_SAMPLE == 33 || CULL_SAMPLE == 34)
                draw->AddRect({p.left, p.top}, {p.right, p.bottom}, color);
            if (CULL_SAMPLE == 34 && i == size_t(inspectObject))
            {
                unsigned x0 = unsigned(p.left), y0 = unsigned(p.top), x1 = unsigned(p.right) - 1,
                         y1 = unsigned(p.bottom) - 1, level = 0, scale = 1;
                while (x1 - x0 > 1 || y1 - y0 > 1)
                {
                    x0 /= 2;
                    y0 /= 2;
                    x1 /= 2;
                    y1 /= 2;
                    ++level;
                    scale *= 2;
                }
                draw->AddRect({float(x0 * scale), float(y0 * scale)},
                              {float((x1 + 1) * scale), float((y1 + 1) * scale)}, IM_COL32_WHITE, 0.f, 3.f,
                              ImDrawFlags_None);
                char label[80];
                snprintf(label, sizeof(label), "Object %u: mip %u, %u x %u cells", unsigned(i), level,
                         x1 - x0 + 1, y1 - y0 + 1);
                draw->AddText({p.left, p.top}, IM_COL32_WHITE, label);
            }
        }
        if (culling.freeze)
        {
            auto inverse = XMMatrixInverse(nullptr, XMLoadFloat4x4(&culling.viewProjection));
            std::array<XMVECTOR, 8> corners;
            for (int k = 0; k < 8; ++k)
                corners[k] = XMVector3TransformCoord(
                    XMVectorSet(k & 1 ? 1.f : -1.f, k & 2 ? 1.f : -1.f, k & 4 ? 1.f : 0.f, 1), inverse);
            drawEdges(corners, IM_COL32(255, 240, 80, 255), 2);
        }
    }
};
} // namespace tutorial
