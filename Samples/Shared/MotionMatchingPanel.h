#pragma once
#include "CharacterInput.h"
#include "MotionMatchingController.h"
#include "TraversalController.h"
#include "SkinningPanel.h"
#include <fstream>

namespace tutorial
{
// Shared CPU scene, input and widgets; all graphics resources remain in each Sample.
struct MotionMatchingPanel : SkinningPanel
{
    TraversalController character;
    CharacterInput input;
    bool testCharacter = false;
    nlohmann::json trace = nlohmann::json::array();
    std::filesystem::path tracePath, recordDirectory;
    int stage = 29;
    explicit MotionMatchingPanel(int value=29):stage(value){}
    void Initialize(Workshop &w, const Options &o, SkinningData &data)
    {
        if(stage>=30)MergeCourse(w,data);
        auto initOptions=o; initOptions.animation=L"MM_Idle";
        SkinningPanel::Initialize(w, initOptions, data.model);
        character.Initialize(data.model, (o.model.empty()?AssetPath(L"Assets/Models/SilverRobotMotionSet/SilverRobot.gltf"):o.model).parent_path(),stage);
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
        if(stage==29){w.scenes[0].minimum = {-15, -.05f, -15};w.scenes[0].maximum = {15, 4, 15};AddFloorMarks(w, data);}
        UpdateCamera(w);
    }
    void MergeCourse(Workshop &w,SkinningData &data)
    {
        auto course=LoadWorkshop(AssetPath(L"Assets/Models/TraversalCourse/Scene.json"));
        const uint32_t images=uint32_t(w.images.size()),materials=uint32_t(w.materials.size()),primitives=uint32_t(w.primitives.size());
        w.images.insert(w.images.end(),course.images.begin(),course.images.end());
        for(auto material:course.materials){for(auto &i:material.images)i+=images;w.materials.push_back(material);}
        for(auto primitive:course.primitives){primitive.material+=materials;data.influences.insert(data.influences.end(),primitive.vertices.size(),SkinInfluence{});w.primitives.push_back(std::move(primitive));}
        auto courseScene=course.scenes[0];for(auto &i:courseScene.instances)i.primitive+=primitives;
        for(const auto &i:w.scenes[0].instances)if(i.primitive<data.model.primitives.size())courseScene.instances.push_back(i);
        courseScene.minimum.y=-4;courseScene.maximum.y=6;w.scenes[0]=std::move(courseScene);
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
        if(frame>=130 && frame<200){c.move.x=1;c.move.y=0;}
        if(frame>=200 && frame<240)c.move={0,-1};
        c.jump = false;
        return c;
    }
    void AdvanceCharacter(double dt, HWND window, Workshop &w)
    {
        ++frame;
        auto command = testCharacter ? TestCommand() : input.Poll(window);
        if(testCharacter && stage>=30)
        {
            command={};if(frame<840 && frame>1)command.move={0,1};command.reset=frame==1 || frame==870;
            command.jump=stage==30 && frame==140;
        }
        character.Advance(dt, command, testCharacter || input.active);
        animation = int(character.Clip());
        animationTime = character.stateTime;
        UpdateCamera(w);
        if (testCharacter)
        {
            const auto &p = character.position;
            trace.push_back({{"frame", frame}, {"state", character.Name()}, {"position", {p.x, p.y, p.z}},
                             {"speed", character.Speed()}, {"grounded", character.Grounded()},
                             {"yaw", character.yaw}, {"cameraYaw", character.cameraYaw},
                             {"searches",character.searches},{"switches",character.switches},{"clipTime",character.playbackTime},
                             {"searchCost",character.selectedCost},{"searchMs",character.searchMilliseconds},{"footError",character.footError},
                             {"reason",character.reason},{"respawns",character.respawns},{"penetration",character.penetration}});
        }
    }
    std::filesystem::path CharacterCapture() const
    {
        if (!testCharacter || screenshot.empty()) return {};
        if(stage>=30)
        {
            std::wstring label;
            if(frame==1)label=L"baseline";
            else if(frame==100)label=L"walk";
            else if(frame==160 && stage==30)label=L"jump";
            else if(frame==225)label=L"land";
            else if(frame==600)label=L"wall";
            else if(frame==870)label=L"reset";
            if(label.empty() || !captures_.insert(label).second)return {};
            return screenshot.parent_path()/(screenshot.stem().wstring()+L"-"+label+L".png");
        }
        const wchar_t *name = frame == 1 ? L"baseline" : frame == 75 ? L"walk" : frame == 120 ? L"run" :
                              frame == 155 ? L"turn" :
                              frame == 270 ? L"reset" : nullptr;
        return name ? screenshot.parent_path() / (screenshot.stem().wstring() + L"-" + name + L".png") : std::filesystem::path{};
    }
    mutable std::set<std::wstring> captures_;
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
    void DrawTrajectory()
    {
        ImGui::TextUnformatted("Top view: desired cyan / matched orange");
        auto origin=ImGui::GetCursorScreenPos(); auto *draw=ImGui::GetWindowDrawList();
        ImGui::Dummy({300,70});
        auto point=[&](DirectX::XMFLOAT3 p){return ImVec2{origin.x+150+(p.x-character.position.x)*20,origin.y+55+(p.z-character.position.z)*20};};
        for(int kind=0;kind<2;++kind){auto prev=point(character.position);const auto &path=kind?character.matchedTrajectory:character.desiredTrajectory;for(auto p:path){auto next=point(p);draw->AddLine(prev,next,kind?IM_COL32(255,160,50,255):IM_COL32(40,230,245,255),2);draw->AddCircleFilled(next,3,IM_COL32(255,255,255,255));prev=next;}}
        for(size_t side=0;side<2;++side)if(character.FootLocked(side))draw->AddCircleFilled(point(character.FootAnchor(side)),4,IM_COL32(80,255,120,255));
    }
    void DrawCharacter()
    {
        if (!visible) return;
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({365, 550}, ImGuiCond_Always);
        ImGui::Begin(stage==29?"Motion Matching":"Traversal Controller");
        ImGui::Text("Stage %d",stage);
        ImGui::TextUnformatted("WASD / Left stick: move");
        ImGui::TextUnformatted("Shift / Hold L3: run");
        if(stage==30)ImGui::TextUnformatted("Space / A: jump");
        ImGui::TextUnformatted("RMB drag / Right stick: orbit");
        ImGui::TextUnformatted("R / View: reset    Esc: quit");
        ImGui::Separator();
        if (input.selected < XUSER_MAX_COUNT) ImGui::Text("XInput: player %u", unsigned(input.selected + 1));
        else ImGui::TextUnformatted("XInput: disconnected (keyboard ready)");
        ImGui::Text("State: %s / %s", character.Name(), character.Grounded() ? "Grounded" : "Airborne");
        ImGui::Text("Speed: %.2f m/s", character.Speed());
        ImGui::Text("Position: %.2f, %.2f, %.2f", character.position.x, character.position.y, character.position.z);
        if (ImGui::Button("Reset")) { character.Reset(); input.Clear(); }
        ImGui::Text("Search %.3f ms / cost %.3f",character.searchMilliseconds,character.selectedCost);
        const auto &clip=character.database.clips[character.selected];const float at=clip.loop?std::fmod(character.playbackTime,clip.duration):std::min(character.playbackTime,clip.duration);
        ImGui::Text("Clip frame %u / %u",unsigned(at*30),unsigned(clip.roots.size()-1));
        ImGui::Text("Pose/path/speed: %.3f / %.3f / %.3f",character.costParts[0],character.costParts[1],character.costParts[2]);
        ImGui::Text("Searches %llu / transitions %llu",character.searches,character.switches);
        ImGui::Text("Foot residual %.3f m",character.footError);
        if(stage==30){ImGui::Text("Penetration %.4f m / respawns %llu",character.penetration,character.respawns);ImGui::TextUnformatted(character.reason.c_str());}
        ImGui::PushItemWidth(185);
        ImGui::SliderFloat3("Pose/path/speed",character.weights.data(),0,6);
        ImGui::PopItemWidth();
        ImGui::Checkbox("Inertialization",&character.inertialization);
        ImGui::Checkbox("Foot IK",&character.footIk);
        DrawTrajectory();
        ImGui::Checkbox("Shadow", &shadows);
        ImGui::SameLine(); ImGui::Checkbox("IBL", &ibl);
        ImGui::End();
    }
};
} // namespace tutorial
