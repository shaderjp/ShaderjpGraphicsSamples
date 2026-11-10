#pragma once
#include "DepthData.h"
#include <cstdio>
#include <imgui.h>

namespace tutorial::depth
{
inline void ConfigureFont()
{
    auto &io = ImGui::GetIO();
    static ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
    builder.AddText(
        "、。あいかがけこさしじすせたづてでとなにのはぶへまみらりるれわをんァアイカクスセッテトバビフポメラリ"
        "ル・上両中低使保倍値側傾像元光分初判別前動化取受右同向図型基変外子存定実察対左差布常平度式形影後復心"
        "必応投択接描換数斜方明時景暗曲有期本模横橙比法深測点無理用画白的直矩示精素線義背蔽行表要見視観解設誤"
        "説読論象起距軸較近透途通遠適遮選部量間閾限隔隣離非面験黒（）：");
    builder.BuildRanges(&ranges);
    const auto file = AssetPath(L"Assets/Fonts/BIZUDGothic-Regular.ttf").string();
    if (!io.Fonts->AddFontFromFileTTF(file.c_str(), 20, nullptr, ranges.Data))
        throw std::runtime_error("Bundled BIZ UD Gothic font is missing");
}
struct Panel
{
    Settings settings, initial, measuredSettings;
    bool shadow = DEPTH_SAMPLE == 36, hidden = false, test = false, captureGpu = false, logAxis = false;
    bool supported[3] = {true, true, true}, dirty = true;
    int frame = 0, graph = 0;
    float probeU = .5f, probeV = .5f, scale = 1;
    Rect cells[6];
    ImVec2 editPoint{}, resetPoint{};
    std::filesystem::path screenshot;
    std::array<Pixel, 2> probes{};
    Pixel lightProbe{};
    ImVec2 lightUv{-.1f, -.1f};
    float receiverZ = 0, receiverProjected = 0, receiverLinear = 0, radial = 0;
    void Initialize(const Options &o)
    {
        settings.Parse(o, shadow);
        initial = settings;
        hidden = o.hideUi;
        test = o.testUi;
        captureGpu = o.captureGpu;
        screenshot = o.screenshot;
        graph = settings.graph;
    }
    void Layout(uint32_t w, uint32_t h)
    {
        scale = std::min(float(w) / 1920, float(h) / 1080);
        const float left = 350 * scale, gap = 20 * scale, cw = std::max(16.f, (w - left - 3 * gap) / 2);
        const float sceneH = std::max(16.f, 350 * scale), smallH = 110 * scale;
        for (int p = 0; p < 2; ++p)
        {
            cells[p] = {left + gap + p * (cw + gap), 112 * scale, cw, sceneH};
            cells[2 + p] = {cells[p].x, 505 * scale, cw, smallH};
            cells[4 + p] = {cells[p].x, 665 * scale, cw, smallH};
        }
    }
    void QueueTestInput()
    {
        if (!test)
            return;
        auto &io = ImGui::GetIO();
        if (frame == 16 || frame == 32 || frame == 44)
        {
            auto p = frame == 16   ? editPoint
                     : frame == 32 ? resetPoint
                                   : ImVec2{cells[0].x + cells[0].w * .62f, cells[0].y + cells[0].h * .67f};
            io.AddMousePosEvent(p.x, p.y);
            io.AddMouseButtonEvent(0, true);
        }
        if (frame == 17 || frame == 33 || frame == 45)
            io.AddMouseButtonEvent(0, false);
    }
    std::filesystem::path TestCapture() const
    {
        if (!test || screenshot.empty())
            return {};
        const wchar_t *suffix = frame == 12   ? L"-baseline"
                                : frame == 26 ? L"-edited"
                                : frame == 42 ? L"-reset"
                                              : nullptr;
        return suffix ? screenshot.parent_path() / (screenshot.stem().wstring() + suffix + L".png")
                      : std::filesystem::path{};
    }
    void Plot(Rect area)
    {
        auto *draw = ImGui::GetWindowDrawList();
        const ImU32 cyan = IM_COL32(63, 201, 245, 255), orange = IM_COL32(255, 164, 83, 255),
                    muted = IM_COL32(135, 157, 183, 255);
        draw->AddRectFilled({area.x, area.y}, {area.x + area.w, area.y + area.h}, IM_COL32(18, 29, 45, 255),
                            8 * scale);
        ImGui::SetCursorScreenPos({area.x + 12 * scale, area.y + 8 * scale});
        const bool logarithmic = graph != 0 || logAxis;
        ImGui::TextUnformatted(graph == 0 ? (logarithmic ? "変換曲線  d(z)  / 横軸: 深度 z [m]（対数）"
                                                         : "変換曲線  d(z)  / 横軸: 深度 z [m]（線形）")
                               : graph == 1
                                   ? "隣の保存値までの間隔 [m]  / 両軸: 対数（保存形式の理論値）"
                                   : "4-bit UNORM / E3M3 float の説明模型（実GPU形式ではありません）");
        const float x = area.x + 70 * scale, y = area.y + 40 * scale, w = area.w - 90 * scale,
                    h = area.h - 72 * scale;
        auto m = settings.Map(false);
        if (shadow)
        {
            m.orthographic = !settings.spot;
            m.infinite = false;
        }
        const double end =
            std::max(m.infinite ? double(settings.displayFar) : std::min(double(settings.displayFar), m.farZ),
                     m.nearZ * 1.01);
        auto position = [&](double t)
        { return logarithmic ? m.nearZ * std::pow(end / m.nearZ, t) : m.nearZ + (end - m.nearZ) * t; };
        auto px = [&](double z)
        {
            return x + w * float(logarithmic ? std::log(z / m.nearZ) / std::log(end / m.nearZ)
                                             : (z - m.nearZ) / (end - m.nearZ));
        };
        for (int i = 0; i <= 4; ++i)
        {
            float xx = x + w * i / 4;
            draw->AddLine({xx, y}, {xx, y + h}, IM_COL32(44, 59, 77, 255));
            char label[40];
            std::snprintf(label, sizeof(label), "%.3g m", position(i / 4.0));
            float labelWidth = ImGui::CalcTextSize(label).x;
            draw->AddText({std::clamp(xx - labelWidth * .5f, x, area.x + area.w - labelWidth - 8 * scale),
                           y + h + 6 * scale},
                          muted, label);
        }
        if (graph == 2)
        {
            for (int p = 0; p < 2; ++p)
            {
                m.reversed = p == 1;
                const float yy = y + (p ? .7f : .2f) * h;
                for (int kind = 0; kind < 2; ++kind)
                {
                    auto values = ToyFloat();
                    if (kind == 0)
                    {
                        values.clear();
                        for (int i = 0; i < 16; ++i)
                            values.push_back(i / 15.0);
                    }
                    for (double d : values)
                    {
                        double z = m.Decode(d);
                        if (z >= m.nearZ && z <= end)
                            draw->AddLine({px(z), yy + kind * 12 * scale},
                                          {px(z), yy + (kind * 12 + 8) * scale}, kind ? orange : cyan, 2);
                    }
                }
                draw->AddText({x, y + (p ? .5f : 0) * h}, muted,
                              p ? "Reverse-Z: cyan=UNORM / orange=float"
                                : "通常Z: cyan=UNORM / orange=float");
            }
            return;
        }
        for (int p = 0; p < 2; ++p)
        {
            m.reversed = !shadow && p == 1;
            if (shadow && graph == 1)
                m.orthographic = p == 1 || !settings.spot;
            ImVec2 previous{};
            bool valid = false;
            for (int i = 0; i < 400; ++i)
            {
                double z = position(i / 399.0);
                double value = graph == 0
                                   ? (shadow && p == 1 ? (z - m.nearZ) / (m.farZ - m.nearZ) : m.Encode(z))
                                   : Spacing(m, z, settings.format);
                if (graph == 1)
                    value = (std::log10(std::max(value, 1e-10)) + 10) / 13;
                if (!std::isfinite(value))
                {
                    valid = false;
                    continue;
                }
                ImVec2 next{x + w * i / 399, y + h * (1 - float(std::clamp(value, 0.0, 1.0)))};
                if (valid)
                    draw->AddLine(previous, next, p ? orange : cyan, 2 * scale);
                previous = next;
                valid = true;
            }
        }
        for (int i = 0; i <= 2; ++i)
        {
            char label[30];
            std::snprintf(label, sizeof(label), graph == 1 ? "1e%.1f" : "%.1f",
                          graph == 1 ? 3 - i * 6.5 : 1 - i * .5);
            draw->AddText({area.x + 4 * scale, y + i * h / 2}, muted, label);
        }
        if (probes[0].coverage > 0)
        {
            float xx = px(probes[0].z);
            if (xx >= x && xx <= x + w)
                draw->AddLine({xx, y}, {xx, y + h}, IM_COL32(255, 255, 255, 200), 1);
        }
    }
    void Draw(uint32_t width, uint32_t height, const char *api)
    {
        Layout(width, height);
        ++frame;
        if (hidden)
            return;
        ImGui::GetIO().FontGlobalScale = scale;
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({float(width), float(height)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::Begin("Depth Laboratory", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        auto *d = ImGui::GetWindowDrawList();
        const auto cyan = IM_COL32(63, 201, 245, 255), orange = IM_COL32(255, 164, 83, 255),
                   white = IM_COL32(226, 235, 247, 255);
        d->AddRectFilled({0, 0}, {float(width), 76 * scale}, IM_COL32(19, 31, 48, 255));
        d->AddText(nullptr, 29 * scale, {22 * scale, 18 * scale}, white,
                   shadow ? "36  Shadow Mapの深度を比較する" : "35  深度バッファを見て、動かして理解する");
        char badge[80];
        std::snprintf(badge, sizeof(badge), "%s  /  GPU rasterization", api);
        d->AddText({width - 365 * scale, 26 * scale}, cyan, badge);
        ImGui::SetCursorScreenPos({18 * scale, 90 * scale});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10 * scale, 8 * scale});
        ImGui::BeginChild("controls", {320 * scale, height - 108 * scale}, ImGuiChildFlags_Borders);
        ImGui::PopStyleVar();
        ImGui::TextUnformatted("実験を選ぶ");
        const char *presets35[] = {"基本：保存値と距離", "Nearを近づける", "遠方の近接面", "無限遠の投影",
                                   "平行投影"};
        const char *presets36[] = {"方向光：平行投影", "スポットライト：透視投影", "バイアスなし",
                                   "解像度 256"};
        int p = settings.preset;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##preset", &p, shadow ? presets36 : presets35, shadow ? 4 : 5))
        {
            settings.Preset(p, shadow);
            dirty = true;
        }
        ImGui::Separator();
        ImGui::PushItemWidth(-1);
        if (!shadow)
        {
            ImGui::TextUnformatted("保存形式");
            const char *names[] = {"D16 UNORM", "D24 UNORM", "D32 FLOAT"};
            for (int i = 0; i < 3; ++i)
            {
                ImGui::BeginDisabled(!supported[i]);
                if (ImGui::RadioButton(names[i], settings.format == i))
                {
                    settings.format = i;
                    dirty = true;
                }
                ImGui::EndDisabled();
                if (!supported[i] && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("このGPUでは必要な描画・読み取り用途に非対応");
            }
            dirty |= ImGui::Checkbox("平行投影", &settings.orthographic);
            if (settings.orthographic)
                settings.infinite = false;
            ImGui::BeginDisabled(settings.orthographic);
            dirty |= ImGui::Checkbox("無限遠（透視投影）", &settings.infinite);
            ImGui::EndDisabled();
        }
        else
        {
            dirty |= ImGui::Checkbox("スポットライト", &settings.spot);
            const char *enc[] = {"投影深度", "明示的な線形深度", "左右で比較"};
            dirty |= ImGui::Combo("##encoding", &settings.encoding, enc, 3);
            dirty |= ImGui::Checkbox("3 x 3 PCF", &settings.pcf);
            ImGui::TextUnformatted("受光点のバイアス [m]");
            dirty |=
                ImGui::SliderFloat("##bias", &settings.bias, 0, .3f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
            int r = settings.resolution == 256 ? 0 : settings.resolution == 1024 ? 1 : 2;
            const char *resolutions[] = {"256", "1024", "2048"};
            if (ImGui::Combo("##resolution", &r, resolutions, 3))
            {
                settings.resolution = r == 0 ? 256 : r == 1 ? 1024 : 2048;
                dirty = true;
            }
        }
        ImGui::TextUnformatted("Near [m]");
        dirty |= ImGui::SliderFloat("##near", &settings.nearZ, .001f, 5, "%.4f",
                                    ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        ImGui::BeginDisabled(settings.infinite);
        ImGui::TextUnformatted("Far [m]");
        dirty |= ImGui::SliderFloat("##far", &settings.farZ, 10, 100000, "%.1f",
                                    ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        settings.farZ = std::max(settings.farZ, settings.nearZ * 2);
        ImGui::TextUnformatted("線形表示の上限 [m]");
        dirty |= ImGui::SliderFloat("##display", &settings.displayFar, 10, 10000, "%.1f",
                                    ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        if (!shadow && settings.stress)
        {
            ImGui::TextUnformatted("遠方の2面の間隔 [m]");
            dirty |= ImGui::SliderFloat("##gap", &settings.gap, .0001f, .1f, "%.5f",
                                        ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        }
        if (ImGui::Button(shadow ? "バイアスを 0.15 m に" : "Nearを10倍にする", {-1, 0}))
        {
            if (shadow)
                settings.bias = .15f;
            else
                settings.nearZ = std::min(settings.nearZ * 10, settings.farZ * .5f);
            dirty = true;
            Trace("Depth UI edited");
        }
        auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        editPoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
        if (ImGui::Button("Reset：起動時の設定へ", {-1, 0}))
        {
            settings = initial;
            graph = initial.graph;
            logAxis = false;
            probeU = probeV = .5f;
            dirty = true;
            Trace("Depth UI reset");
        }
        a = ImGui::GetItemRectMin();
        b = ImGui::GetItemRectMax();
        resetPoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
        ImGui::Separator();
        ImGui::TextWrapped("%s", shadow ? "ライトの投影方法で深度の分布が変わります。影の比較には、保存値と受"
                                          "光点で同じ深度の定義を使います。"
                                        : "保存値 d と深度 z は別の値です。画像をクリックして比較します。");
        // Text entry must not inject NaN/Inf into projection matrices or graph coordinates.
        for (auto pair : {std::pair{&settings.nearZ, initial.nearZ}, std::pair{&settings.farZ, initial.farZ},
                          std::pair{&settings.displayFar, initial.displayFar},
                          std::pair{&settings.gap, initial.gap}, std::pair{&settings.bias, initial.bias}})
            if (!std::isfinite(*pair.first))
            {
                *pair.first = pair.second;
                dirty = true;
            }
        settings.farZ = std::max(settings.farZ, settings.nearZ * 2);
        ImGui::Spacing();
        ImGui::TextUnformatted(shadow ? "選択した受光点（カメラ深度）" : "選択した画素 / GPU readback");
        for (int i = 0; i < (shadow ? 1 : 2); ++i)
        {
            ImGui::TextColored(i ? ImVec4(1, .65f, .33f, 1) : ImVec4(.25f, .8f, 1, 1), "%s",
                               shadow ? (i ? "右" : "左") : (i ? "Reverse-Z" : "通常Z"));
            if (probes[i].coverage == 0)
                ImGui::TextUnformatted("背景（復元の対象外）");
            else
            {
                auto m = measuredSettings.Map(i == 1);
                if (shadow)
                    m = {.1, 100, true, false, false};
                const double z = m.Decode(probes[i].raw);
                ImGui::Text("d = %.9g", probes[i].raw);
                ImGui::Text("z = %.7g m / ID %.0f", z, probes[i].id);
                ImGui::Text("GPU view z = %.7g m", probes[i].z);
            }
        }
        if (probes[0].coverage > 0)
            ImGui::Text("直線距離 = %.6g m", radial);
        if (shadow && lightUv.x >= 0 && lightUv.x < 1 && lightUv.y >= 0 && lightUv.y < 1)
        {
            ImGui::Text("Light z = %.6g m", receiverZ);
            ImGui::Text("保存 d = %.8g", lightProbe.raw);
            ImGui::Text("比較 d = %.8g", receiverProjected);
            ImGui::Text("保存 linear = %.8g", lightProbe.z);
            ImGui::Text("比較 linear = %.8g", receiverLinear);
            ImGui::TextUnformatted("比較値はバイアス適用後。");
            ImGui::TextUnformatted("PCF前の中心テクセルを表示。");
        }
        if (!shadow)
        {
            ImGui::Spacing();
            ImGui::TextUnformatted("カメラの側面図（模式図）");
            auto origin = ImGui::GetCursorScreenPos();
            auto *lines = ImGui::GetWindowDrawList();
            float length = 270 * scale, mid = origin.y + 33 * scale;
            ImVec2 eye{origin.x + 8 * scale, mid}, farTop{origin.x + length, origin.y + 3 * scale},
                farBottom{origin.x + length, origin.y + 63 * scale};
            if (!settings.orthographic)
            {
                lines->AddLine(eye, farTop, cyan);
                lines->AddLine(eye, farBottom, cyan);
            }
            lines->AddLine(eye, {farTop.x, mid}, IM_COL32(140, 155, 175, 255));
            if (settings.orthographic)
            {
                lines->AddLine({eye.x, farTop.y}, farTop, cyan);
                lines->AddLine({eye.x, farBottom.y}, farBottom, cyan);
            }
            float marker = origin.x + std::clamp(probes[0].z / settings.displayFar, 0.f, 1.f) * length;
            lines->AddLine({marker, origin.y}, {marker, origin.y + 66 * scale}, orange, 2 * scale);
            lines->AddCircleFilled(eye, 4 * scale, white);
            ImGui::Dummy({length, 70 * scale});
            ImGui::TextUnformatted("橙線：選択した面の深度 z");
        }
        ImGui::PopItemWidth();
        ImGui::EndChild();
        const char *titles35[] = {"通常Z：Near=0 / Far=1",
                                  "Reverse-Z：Near=1 / Far=0",
                                  "保存された深度 d（0=黒 / 1=白）",
                                  "保存された深度 d（0=黒 / 1=白）",
                                  "保存値から復元した深度 z / 表示上限",
                                  "保存値から復元した深度 z / 表示上限"};
        const char *titles36[] = {"投影深度で影を比較",
                                  "線形深度で影を比較",
                                  "ライトの投影深度 d（0=黒 / 1=白）",
                                  "明示的に保存した線形深度",
                                  "ライト視点：復元した深度 z / 表示上限",
                                  "受光点：遮蔽の判定（暗い部分が影）"};
        for (int i = 0; i < 6; ++i)
        {
            auto r = cells[i];
            const char *label = shadow ? titles36[i] : titles35[i];
            if (shadow && i < 2 && settings.encoding != 2)
                label = settings.encoding == 0 ? "投影深度で影を比較" : "線形深度で影を比較";
            d->AddText({r.x, r.y - 29 * scale}, i % 2 ? orange : cyan, label);
            d->AddRect({r.x - 1, r.y - 1}, {r.x + r.w + 1, r.y + r.h + 1}, IM_COL32(65, 85, 107, 255),
                       3 * scale);
            ImGui::SetCursorScreenPos({r.x, r.y});
            ImGui::PushID(i);
            ImGui::InvisibleButton("probe", {r.w, r.h});
            if (ImGui::IsItemClicked() && (!shadow || i < 2))
            {
                auto p0 = ImGui::GetIO().MousePos;
                probeU = std::clamp((p0.x - r.x) / r.w, 0.f, .9999f);
                probeV = std::clamp((p0.y - r.y) / r.h, 0.f, .9999f);
                dirty = true;
                Trace("Depth probe selected");
            }
            ImGui::PopID();
            if (i < 2 || !shadow)
            {
                ImVec2 p0{r.x + probeU * r.w, r.y + probeV * r.h};
                d->AddCircle(p0, 6 * scale, white, 16, 1.5f);
            }
            else if (i < 5 && lightUv.x >= 0 && lightUv.x < 1 && lightUv.y >= 0 && lightUv.y < 1)
                d->AddCircle({r.x + lightUv.x * r.w, r.y + lightUv.y * r.h}, 6 * scale, orange, 16, 1.5f);
        }
        ImGui::SetCursorScreenPos({370 * scale, 794 * scale});
        ImGui::RadioButton("変換曲線", &graph, 0);
        ImGui::SameLine();
        ImGui::RadioButton("精度の間隔", &graph, 1);
        ImGui::SameLine();
        ImGui::BeginDisabled(shadow);
        ImGui::RadioButton("低ビットの模型", &graph, 2);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(graph != 0);
        ImGui::Checkbox("横軸を対数にする", &logAxis);
        ImGui::EndDisabled();
        Plot({370 * scale, 834 * scale, width - 390 * scale, 228 * scale});
        ImGui::End();
        ImGui::PopStyleVar();
    }
    void UpdateProbes(const std::array<std::vector<Pixel>, 2> &data, uint32_t w, uint32_t h,
                      const std::vector<Pixel> &light)
    {
        measuredSettings = settings; // Parameters belonging to this completed GPU readback.
        size_t index =
            std::min(size_t(h - 1), size_t(probeV * h)) * w + std::min(size_t(w - 1), size_t(probeU * w));
        for (int p = 0; p < 2; ++p)
            if (index < data[p].size())
                probes[p] = data[p][index];
        if (probes[0].coverage == 0)
        {
            lightUv = {-1, -1};
            return;
        }
        auto c = MakeConstants(settings, shadow, 0, w, h);
        float x = (2 * (float(index % w) + .5f) / w - 1), y = 1 - 2 * (float(index / w) + .5f) / h;
        Mapping camera = shadow ? Mapping{.1, 100, true, false, false} : settings.Map(false);
        double z = camera.Decode(probes[0].raw);
        auto view = XMVectorSet(float(x * (camera.orthographic ? 1 : z) / c.projection._11),
                                float(y * (camera.orthographic ? 1 : z) / c.projection._22), float(z), 1);
        auto world = XMVector3TransformCoord(view, XMMatrixInverse(nullptr, XMLoadFloat4x4(&c.view)));
        radial = XMVectorGetX(XMVector3Length(view));
        if (!shadow)
            return;
        auto lightView = XMVector3TransformCoord(world, XMLoadFloat4x4(&c.lightView));
        receiverZ = XMVectorGetZ(lightView);
        auto ndc = XMVector3TransformCoord(lightView, XMLoadFloat4x4(&c.lightProjection));
        lightUv = {XMVectorGetX(ndc) * .5f + .5f, .5f - XMVectorGetY(ndc) * .5f};
        if (receiverZ <= 0 || lightUv.x < 0 || lightUv.x >= 1 || lightUv.y < 0 || lightUv.y >= 1)
        {
            lightUv = {-1, -1};
            return;
        }
        size_t texel = size_t(lightUv.y * settings.resolution) * settings.resolution +
                       size_t(lightUv.x * settings.resolution);
        if (texel < light.size())
            lightProbe = light[texel];
        float biasedZ = std::max(settings.nearZ, receiverZ - settings.bias);
        receiverProjected =
            float(Mapping{settings.nearZ, settings.farZ, false, !settings.spot, false}.Encode(biasedZ));
        receiverLinear = (biasedZ - settings.nearZ) / (settings.farZ - settings.nearZ);
    }
};
} // namespace tutorial::depth
