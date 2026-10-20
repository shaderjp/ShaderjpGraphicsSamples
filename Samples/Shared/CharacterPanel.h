#pragma once
#include "CharacterInput.h"
#include "SkinningPanel.h"
#include <fstream>

namespace tutorial
{
// Shared CPU scene, input and widgets; all graphics resources remain in each Sample.
struct CharacterPanel : SkinningPanel
{
    CharacterController character;
    CharacterInput input;
    bool testCharacter = false;
    nlohmann::json trace = nlohmann::json::array();
    std::filesystem::path tracePath, recordDirectory;
    void Initialize(Workshop &w, const Options &o, SkinningData &data)
    {
        SkinningPanel::Initialize(w, o, data.model);
        character.Initialize(data.model);
        animation = int(character.Clip());
        scene = camera = renderPath = culling = 0;
        pathEnabled = pathPlaying = rootMotion = false;
        testCharacter = o.testCharacter;
        const DWORD count = GetEnvironmentVariableW(L"SHADERJP_CHARACTER_FRAMES", nullptr, 0);
        if (testCharacter && count)
        {
            std::vector<wchar_t> value(count);
            GetEnvironmentVariableW(L"SHADERJP_CHARACTER_FRAMES", value.data(), count);
            recordDirectory = value.data();
            std::filesystem::create_directories(recordDirectory);
        }
        if (!o.log.empty()) tracePath = o.log.parent_path() / (o.log.stem().wstring() + L".character.json");
        w.scenes.resize(1);
        w.scenes[0].name = "Character training floor";
        w.scenes[0].minimum = {-15, -.05f, -15};
        w.scenes[0].maximum = {15, 4, 15};
        AddFloorMarks(w, data);
        UpdateCamera(w);
    }
    void AddFloorMarks(Workshop &w, SkinningData &data)
    {
        using namespace DirectX;
        auto add = [&](const char *name, XMFLOAT4 color)
        {
            auto material = w.materials.back();
            material.id = name;
            material.base = color;
            material.factors = {0, .8f, 0, 1};
            w.materials.push_back(material);
            WorkshopPrimitive p;
            p.id = name;
            p.material = uint32_t(w.materials.size() - 1);
            p.minimum = {-15, 0, -15}; p.maximum = {15, .002f, 15};
            return p;
        };
        auto rect = [](WorkshopPrimitive &p, float x0, float z0, float x1, float z1)
        {
            uint32_t b = uint32_t(p.vertices.size());
            for (auto v : {XMFLOAT3{x0, .002f, z0}, XMFLOAT3{x0, .002f, z1}, XMFLOAT3{x1, .002f, z1}, XMFLOAT3{x1, .002f, z0}})
                p.vertices.push_back({v, {0, 0}, {0, 1, 0}, {1, 0, 0, 1}});
            for (auto i : {0u, 1u, 2u, 0u, 2u, 3u}) p.indices.push_back(b + i);
        };
        auto floorGrid = add("One metre grid", {.24f, .29f, .36f, 1});
        for (int i = -14; i <= 14; ++i)
        {
            if (i == 0) continue;
            rect(floorGrid, float(i) - .015f, -15, float(i) + .015f, 15);
            rect(floorGrid, -15, float(i) - .015f, 15, float(i) + .015f);
        }
        auto axis = add("Origin and boundary", {.65f, .3f, .08f, 1});
        rect(axis, -.025f, -15, .025f, 15);
        rect(axis, -15, -.025f, 15, .025f);
        for (float b : {-14.1f, 14.1f})
        {
            rect(axis, b - .025f, -14.1f, b + .025f, 14.1f);
            rect(axis, -14.1f, b - .025f, 14.1f, b + .025f);
        }
        for (auto *p : {&floorGrid, &axis})
        {
            data.influences.insert(data.influences.end(), p->vertices.size(), SkinInfluence{});
            WorkshopInstance instance;
            instance.id = p->id;
            instance.primitive = uint32_t(w.primitives.size());
            XMStoreFloat4x4(&instance.world, XMMatrixIdentity());
            w.scenes[0].instances.push_back(instance);
            w.primitives.push_back(std::move(*p));
        }
    }
    void UpdateCamera(Workshop &w)
    {
        w.cameras[0].position = character.cameraEye;
        w.cameras[0].target = character.cameraTarget;
        w.cameras[0].fov = 50;
    }
    CharacterCommand TestCommand() const
    {
        CharacterCommand c;
        c.reset = frame == 1 || frame == 270;
        if (frame >= 30 && frame < 225) c.move.y = 1;
        c.run = frame >= 90 && frame < 130;
        if (frame >= 130 && frame < 160) c.lookVelocity.x = -2;
        c.jump = frame == 160;
        return c;
    }
    void AdvanceCharacter(double dt, HWND window, Workshop &w)
    {
        ++frame;
        const auto command = testCharacter ? TestCommand() : input.Poll(window);
        character.Advance(dt, command, testCharacter || input.active);
        animation = int(character.Clip());
        animationTime = character.stateTime;
        UpdateCamera(w);
        if (testCharacter)
        {
            const auto &p = character.position;
            trace.push_back({{"frame", frame}, {"state", character.Name()}, {"position", {p.x, p.y, p.z}},
                             {"speed", character.Speed()}, {"grounded", character.Grounded()},
                             {"yaw", character.yaw}, {"cameraYaw", character.cameraYaw}});
        }
    }
    std::filesystem::path CharacterCapture() const
    {
        if (!testCharacter || screenshot.empty()) return {};
        const wchar_t *name = frame == 1 ? L"baseline" : frame == 75 ? L"walk" : frame == 120 ? L"run" :
                              frame == 155 ? L"turn" : frame == 190 ? L"jump" : frame == 222 ? L"land" :
                              frame == 270 ? L"reset" : nullptr;
        return name ? screenshot.parent_path() / (screenshot.stem().wstring() + L"-" + name + L".png") : std::filesystem::path{};
    }
    std::filesystem::path FrameCapture() const
    {
        if (recordDirectory.empty() || !frame) return {};
        auto number = std::to_wstring(frame);
        return recordDirectory / (std::wstring(number.size() < 6 ? 6 - number.size() : 0, L'0') + number + L".png");
    }
    void WriteTrace() const
    {
        if (testCharacter && !tracePath.empty())
        {
            std::ofstream output(tracePath);
            if (!output || !(output << trace.dump(2))) throw std::runtime_error("Cannot write character trace");
        }
    }
    void UpdateSkin(SkinningData &data, std::vector<MeshInstance> &instances)
    {
        using namespace DirectX;
        const auto start = std::chrono::steady_clock::now();
        const auto pose = MakeGltfGlobalPose(character.localPose);
        const auto palette = MakeSkinPalette(data.model, pose, 0);
        std::copy(palette.begin(), palette.end(), data.palettes.begin());
        const auto mesh = XMLoadFloat4x4(&pose.globals[data.model.primitives[0].node]);
        for (auto &i : instances)
        {
            if (i.primitive.x >= data.model.primitives.size())
            {
                i.primitive.y = i.primitive.w = UINT32_MAX;
                i.primitive.z = 64 * data.jointCount;
                continue;
            }
            i.primitive.y = i.primitive.z = 0;
            i.primitive.w = UINT32_MAX;
            const auto world = mesh * character.World() * XMLoadFloat4x4(&i.world);
            XMStoreFloat4x4(&i.world, world);
            XMStoreFloat4x4(&i.normal, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
        }
        data.poseMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        data.boundsMilliseconds = 0;
    }
    void DrawCharacter()
    {
        if (!visible) return;
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({315, 290}, ImGuiCond_Always);
        ImGui::Begin("28 Character Controller");
        ImGui::TextUnformatted("WASD / Left stick: move");
        ImGui::TextUnformatted("Shift / Hold L3: run");
        ImGui::TextUnformatted("Space / A: jump");
        ImGui::TextUnformatted("RMB drag / Right stick: orbit");
        ImGui::TextUnformatted("R / View: reset    Esc: quit");
        ImGui::Separator();
        if (input.selected < XUSER_MAX_COUNT) ImGui::Text("XInput: player %u", unsigned(input.selected + 1));
        else ImGui::TextUnformatted("XInput: disconnected (keyboard ready)");
        ImGui::Text("State: %s / %s", character.Name(), character.Grounded() ? "Grounded" : "Airborne");
        ImGui::Text("Speed: %.2f m/s", character.Speed());
        ImGui::Text("Position: %.2f, %.2f, %.2f", character.position.x, character.position.y, character.position.z);
        if (ImGui::Button("Reset")) { character.Reset(); input.Clear(); }
        ImGui::Checkbox("Shadow", &shadows);
        ImGui::SameLine(); ImGui::Checkbox("IBL", &ibl);
        ImGui::End();
    }
};
} // namespace tutorial
