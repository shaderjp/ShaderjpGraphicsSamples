#include "../Samples/Shared/CharacterInput.h"
#include <iostream>
#include <stdexcept>
using namespace tutorial;
using namespace DirectX;
void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
float PoseError(const SkinPose &a, const SkinPose &b)
{
    float result = 0;
    Require(a.globals.size() == b.globals.size(), "Pose size");
    for (size_t i = 0; i < a.globals.size(); ++i)
        for (int k = 0; k < 16; ++k)
            result = std::max(result, std::abs((&a.globals[i]._11)[k] - (&b.globals[i]._11)[k]));
    return result;
}
void CheckCameraHorizontalMovement(const GltfSkinnedModel &model, const CharacterCommand &command, float direction, const char *message)
{
    Require(command.move.x * direction > 0 && command.move.y == 0, "Horizontal input axis sign");
    CharacterController c; c.Initialize(model);
    for (float yaw : {0.f, XM_PIDIV2, -XM_PIDIV2, XM_PI, .7f})
        for (float pitch : {.12f, .3f, 1.25f})
        {
            c.Reset(); c.cameraYaw = yaw; c.cameraPitch = pitch;
            c.Advance(CharacterController::Step, command);
            const auto view = XMMatrixLookAtLH(XMLoadFloat3(&c.cameraEye), XMLoadFloat3(&c.cameraTarget), XMVectorSet(0, 1, 0, 0));
            XMFLOAT3 viewVelocity;
            XMStoreFloat3(&viewVelocity, XMVector3TransformNormal(XMLoadFloat3(&c.velocity), view));
            // Use the renderer's View transform as the oracle, independently of the controller's basis formula.
            Require(viewVelocity.x * direction > 0 && std::abs(viewVelocity.y) < 1e-5f && std::abs(viewVelocity.z) < 1e-5f, message);
            Require(std::abs(viewVelocity.x - command.move.x * CharacterController::WalkSpeed) < 1e-5f, "Horizontal analog speed in camera space");
        }
}
int main(int argc, char **argv)
{
    try
    {
        Require(argc == 2, "Pass repository path");
        const auto model = LoadGltfSkinned(std::filesystem::path(argv[1]) / "Assets/Models/SilverRobotAnimated/SilverRobot.gltf");
        for (size_t i = 0; i < model.clips.size(); ++i)
        {
            const auto a = EvaluateGltfLocalPose(model, i, 0);
            const auto b = EvaluateGltfLocalPose(model, i, model.clips[i].duration * .6f);
            Require(PoseError(MakeGltfGlobalPose(BlendGltfLocalPoses(a, b, 0)), MakeGltfGlobalPose(a)) < 1e-5f, "Blend 0");
            Require(PoseError(MakeGltfGlobalPose(BlendGltfLocalPoses(a, b, 1)), MakeGltfGlobalPose(b)) < 1e-5f, "Blend 1");
        }
        SkinLocalPose a{{SkinNode{}}}, b = a;
        a.nodes[0].translation = {0, 0, 0}; b.nodes[0].translation = {2, 4, 6};
        a.nodes[0].rotation = {0, 0, 0, 1}; b.nodes[0].rotation = {0, 0, 0, -1};
        auto half = BlendGltfLocalPoses(a, b, .5f);
        Require(half.nodes[0].translation.y == 2 && std::abs(half.nodes[0].rotation.w) > .999f, "TRS and quaternion hemisphere");
        a.nodes[0].hasMatrix = true; XMStoreFloat4x4(&a.nodes[0].matrix, XMMatrixTranslation(3, 4, 5));
        Require(MakeGltfGlobalPose(BlendGltfLocalPoses(a, b, .5f)).globals[0]._42 == 4, "Static matrix node");
        Require(CharacterStick(0, 0, 7849).x == 0 && CharacterStick(7849, 0, 7849).x == 0, "Deadzone");
        auto stick = CharacterStick(-32768, -32768, 7849);
        Require(std::abs(std::hypot(stick.x, stick.y) - 1) < 1e-6f, "Stick diagonal and signed minimum");
        Require(CharacterStick(32767, 0, 7849).x == 1, "Stick maximum");
        CharacterPadDecoder decoder;
        XINPUT_GAMEPAD raw{}; raw.wButtons = XINPUT_GAMEPAD_A;
        Require(!decoder.Sample(raw).jump, "Connect with A held");
        raw.wButtons = 0; decoder.Sample(raw); raw.wButtons = XINPUT_GAMEPAD_A;
        Require(decoder.Sample(raw).jump && !decoder.Sample(raw).jump, "Pad edge and hold");
        decoder.Reset(); Require(!decoder.Sample(raw).jump, "Reconnect held button");
        raw.sThumbRX = raw.sThumbRY = 32767;
        const auto orbit = decoder.Sample(raw);
        Require(orbit.lookVelocity.x < 0 && orbit.lookVelocity.y < 0, "Right stick right/up matches mouse look right/up");
        CharacterCommand keyboard, pad;
        keyboard.move.x = -1; pad.move.y = 1; pad.run = true;
        auto combined = CombineCharacterInput(keyboard, pad);
        Require(combined.move.x == -1 && combined.move.y == 0 && !combined.run, "Keyboard priority");
        ImGui::CreateContext();
        CharacterInput input;
        input.Message(nullptr, WM_KEYDOWN, 'W', 0);
        input.Message(nullptr, WM_KEYDOWN, VK_SPACE, 0);
        auto keys = input.ConsumeKeyboard();
        Require(keys.move.y == 1 && keys.jump && !input.ConsumeKeyboard().jump, "Keyboard edge");
        input.Message(nullptr, WM_KILLFOCUS, 0, 0);
        Require(input.ConsumeKeyboard().move.y == 0, "Focus clears movement");
        input.Message(nullptr, WM_KEYDOWN, VK_SPACE, LPARAM(1) << 30);
        Require(!input.ConsumeKeyboard().jump, "Repeat after focus cannot jump");
        ImGui::GetIO().WantCaptureKeyboard = true;
        input.Message(nullptr, WM_KEYDOWN, 'W', 0);
        Require(input.ConsumeKeyboard().move.y == 0, "UI capture");
        ImGui::GetIO().WantCaptureKeyboard = false;
        for (SHORT x : {SHORT(-32768), SHORT(-16000), SHORT(16000), SHORT(32767)})
        {
            CharacterPadDecoder horizontalDecoder;
            XINPUT_GAMEPAD horizontalPad{}; horizontalPad.sThumbLX = x;
            CheckCameraHorizontalMovement(model, horizontalDecoder.Sample(horizontalPad), x > 0 ? 1.f : -1.f,
                                          "Left stick must move toward the corresponding side of the camera");
        }
        for (WPARAM key : {WPARAM('A'), WPARAM('D')})
        {
            input.Clear();
            input.Message(nullptr, WM_KEYDOWN, key, 0);
            CheckCameraHorizontalMovement(model, input.ConsumeKeyboard(), key == 'D' ? 1.f : -1.f,
                                          "A/D must move toward the corresponding side of the camera");
        }
        ImGui::DestroyContext();
        CharacterController c; c.Initialize(model);
        CharacterCommand move; move.move.y = 1;
        for (int i = 0; i < 60; ++i) c.Advance(1.0 / 60, move);
        Require(std::abs(c.position.z + 1.5f) < .001f, "Walk distance and -Z forward");
        c.Reset();
        c.Advance(1.0 / 60, move);
        const auto displayed = c.localPose;
        const float walkPhase = c.phase;
        move.run = true;
        c.Advance(CharacterController::Step, move);
        const auto runPose = EvaluateGltfLocalPose(model, c.Clip(), c.phase * model.clips[c.Clip()].duration);
        const auto expected = BlendGltfLocalPoses(displayed, runPose, float(CharacterController::Step) / CharacterController::BlendSeconds);
        Require(c.phase >= walkPhase && PoseError(MakeGltfGlobalPose(c.localPose), MakeGltfGlobalPose(expected)) < 1e-5f, "Interrupted blend uses displayed pose and preserves phase");
        move.run = false;
        c.Reset(); move.move.x = 1;
        for (int i = 0; i < 60; ++i) c.Advance(1.0 / 60, move);
        Require(std::abs(std::hypot(c.position.x, c.position.z) - 1.5f) < .001f, "Diagonal speed");
        c.Reset(); move.move = {0, 1}; c.cameraYaw = XM_PIDIV2;
        c.Advance(1.0 / 60, move);
        Require(c.position.x < 0 && std::abs(c.position.z) < .0001f, "Camera-relative forward");
        c.Reset(); move.jump = true; c.Advance(CharacterController::Step / 2, move);
        move.jump = false; c.Advance(CharacterController::Step / 2, move);
        Require(!c.Grounded(), "Jump edge survives a frame shorter than one simulation step");
        const float momentum = c.velocity.z;
        CharacterCommand neutral;
        c.Advance(1.0 / 60, neutral);
        Require(c.velocity.z == momentum, "Air inertia without input");
        c.Reset(); move.jump = true; c.Advance(1.0 / 60, move); move.jump = false;
        Require(c.position.y > 0 && c.state == CharacterState::JumpStart, "Immediate jump");
        for (int i = 0; i < 15; ++i) c.Advance(1.0 / 60, move);
        Require(c.state == CharacterState::JumpLoop, "Air loop");
        move.jump = true; const float vy = c.velocity.y; c.Advance(1.0 / 60, move); move.jump = false;
        Require(c.velocity.y < vy, "No air jump");
        while (!c.Grounded()) c.Advance(1.0 / 60, move);
        Require(c.state == CharacterState::JumpLand && c.Speed() > 0, "Land and moving landing");
        move.jump = true; c.Advance(1.0 / 60, move); move.jump = false;
        Require(!c.Grounded(), "Jump allowed during landing");
        const auto before = c.position; c.Advance(10, move, false);
        Require(c.position.y == before.y, "Inactive simulation paused");
        c.Reset(); move.move = {0, 1}; move.run = true;
        for (int i = 0; i < 600; ++i) c.Advance(1.0 / 60, move);
        Require(c.position.z == -14, "Floor boundary");
        float referenceZ = 0, referencePeak = 0;
        for (int fps : {30, 60, 144})
        {
            c.Reset(); move.run = false; float peak = 0;
            for (int i = 0; i < fps * 3; ++i)
            {
                move.jump = i == fps;
                c.Advance(1.0 / fps, move);
                peak = std::max(peak, c.position.y);
            }
            if (fps == 30) { referenceZ = c.position.z; referencePeak = peak; }
            Require(std::abs(c.position.z - referenceZ) < .015f && std::abs(peak - referencePeak) < .005f, "30/60/144 fps trajectory");
        }
        c.Reset(); const auto initial = MakeGltfGlobalPose(c.localPose);
        move.reset = true; c.Advance(.016, move);
        Require(c.position.x == 0 && c.position.z == 0 && PoseError(initial, MakeGltfGlobalPose(c.localPose)) == 0, "Exact reset");
        std::cout << "Character CPU PASS: input, focus, blend, movement, camera-space left/right (XInput and A/D), orbit, jump, landing, reset, 30/60/144 fps\n";
        return 0;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
