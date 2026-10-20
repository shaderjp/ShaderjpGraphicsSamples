#pragma once
#include "SkinnedGltf.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tutorial
{
struct CharacterCommand
{
    DirectX::XMFLOAT2 move{}, lookVelocity{}, orbitDelta{};
    bool run = false, jump = false, reset = false;
};
enum class CharacterState { Idle, Walk, Run, JumpStart, JumpLoop, JumpLand };
inline constexpr const char *CharacterStateNames[] = {"Idle", "Walk", "Run", "JumpStart", "JumpLoop", "JumpLand"};

// CPU simulation only. The renderer consumes a pose and an actor World matrix.
class CharacterController
{
  public:
    static constexpr double Step = 1.0 / 120.0;
    static constexpr float WalkSpeed = 1.5f, RunSpeed = 3.f, Gravity = 9.8f, JumpSpeed = 5.f;
    static constexpr float BlendSeconds = .12f, StartSeconds = .18f, LandSeconds = .16f;
    DirectX::XMFLOAT3 position{}, velocity{}, cameraTarget{0, 1, 0}, cameraEye{0, 2, 4};
    float yaw = 0, cameraYaw = 0, cameraPitch = .3f, stateTime = 0, phase = 0;
    CharacterState state = CharacterState::Idle;
    SkinLocalPose localPose;
    uint64_t ticks = 0;
    bool Grounded() const { return position.y == 0 && velocity.y == 0; }
    float Speed() const { return std::hypot(velocity.x, velocity.z); }
    const char *Name() const { return CharacterStateNames[size_t(state)]; }
    size_t Clip() const { return clips_[size_t(state)]; }
    void Initialize(const GltfSkinnedModel &model)
    {
        model_ = &model;
        for (size_t s = 0; s < clips_.size(); ++s)
        {
            const std::string name = std::string("UAL_") + CharacterStateNames[s];
            auto it = std::find_if(model.clips.begin(), model.clips.end(), [&](const auto &c) { return c.name == name; });
            if (it == model.clips.end() || it->duration <= 0)
                throw std::runtime_error("Character controller requires animation: " + name);
            clips_[s] = size_t(it - model.clips.begin());
        }
        Reset();
    }
    void Reset()
    {
        position = velocity = {};
        cameraTarget = {0, 1, 0};
        yaw = cameraYaw = stateTime = phase = 0;
        cameraPitch = .3f;
        state = CharacterState::Idle;
        accumulator_ = 0;
        pendingJump_ = false;
        ticks = 0;
        blendTime_ = BlendSeconds;
        localPose = EvaluateGltfLocalPose(*model_, Clip(), 0);
        sourcePose_ = localPose;
        UpdateCamera();
    }
    void Advance(double seconds, const CharacterCommand &command, bool active = true)
    {
        if (!active)
        {
            accumulator_ = 0;
            pendingJump_ = false;
            return;
        }
        if (command.reset) { Reset(); return; }
        const float dt = float(std::clamp(seconds, 0.0, .05));
        cameraYaw = std::remainder(cameraYaw + command.orbitDelta.x, DirectX::XM_2PI);
        cameraPitch = std::clamp(cameraPitch + command.orbitDelta.y, .12f, 1.25f);
        pendingJump_ |= command.jump;
        accumulator_ += dt;
        while (accumulator_ + 1e-9 >= Step)
        {
            Tick(command);
            accumulator_ -= Step;
        }
        UpdateCamera();
    }
    DirectX::XMMATRIX World() const
    {
        return DirectX::XMMatrixRotationY(yaw) * DirectX::XMMatrixTranslation(position.x, position.y, position.z);
    }

  private:
    const GltfSkinnedModel *model_ = nullptr;
    std::array<size_t, 6> clips_{};
    SkinLocalPose sourcePose_;
    double accumulator_ = 0;
    float blendTime_ = BlendSeconds;
    bool pendingJump_ = false;
    void Change(CharacterState next)
    {
        if (state == next) return;
        sourcePose_ = localPose; // Interrupted transitions start at the currently displayed pose.
        blendTime_ = 0;
        stateTime = 0;
        state = next;
    }
    void Tick(const CharacterCommand &c)
    {
        using namespace DirectX;
        const float dt = float(Step);
        ++ticks;
        cameraYaw = std::remainder(cameraYaw + c.lookVelocity.x * dt, XM_2PI);
        cameraPitch = std::clamp(cameraPitch + c.lookVelocity.y * dt, .12f, 1.25f);
        const float length = std::hypot(c.move.x, c.move.y);
        const float scale = (c.run ? RunSpeed : WalkSpeed) / std::max(1.f, length);
        const float right = c.move.x * scale, forward = c.move.y * scale;
        // LookAtLH's screen right is cross(world up, view forward): -X at camera yaw zero.
        const float desiredX = -right * std::cos(cameraYaw) - forward * std::sin(cameraYaw);
        const float desiredZ = right * std::sin(cameraYaw) - forward * std::cos(cameraYaw);
        if (Grounded())
        {
            velocity.x = desiredX;
            velocity.z = desiredZ;
        }
        else if (length > 0)
        {
            const float air = 1 - std::exp(-3.f * dt);
            velocity.x += (desiredX - velocity.x) * air;
            velocity.z += (desiredZ - velocity.z) * air;
        }
        if (pendingJump_ && Grounded())
        {
            velocity.y = JumpSpeed;
            Change(CharacterState::JumpStart);
        }
        pendingJump_ = false; // An edge is consumed once, even when multiple substeps run.
        position.x = std::clamp(position.x + velocity.x * dt, -14.f, 14.f);
        position.z = std::clamp(position.z + velocity.z * dt, -14.f, 14.f);
        if (!Grounded())
        {
            position.y += velocity.y * dt - .5f * Gravity * dt * dt;
            velocity.y -= Gravity * dt;
            if (position.y <= 0 && velocity.y < 0)
            {
                position.y = velocity.y = 0;
                Change(CharacterState::JumpLand);
            }
        }
        if (Speed() > .001f)
        {
            const float desiredYaw = std::atan2(-velocity.x, -velocity.z);
            yaw += std::clamp(std::remainder(desiredYaw - yaw, XM_2PI), -8 * dt, 8 * dt);
        }
        stateTime += dt;
        if (Grounded())
        {
            if (state != CharacterState::JumpLand || stateTime >= LandSeconds)
                Change(Speed() < .01f ? CharacterState::Idle : c.run ? CharacterState::Run : CharacterState::Walk);
        }
        else if (state == CharacterState::JumpStart && stateTime >= StartSeconds)
            Change(CharacterState::JumpLoop);
        const auto &clip = model_->clips[Clip()];
        float time = stateTime;
        if (state == CharacterState::Walk || state == CharacterState::Run)
        {
            phase = std::fmod(phase + dt * Speed() / (state == CharacterState::Run ? RunSpeed : WalkSpeed) / clip.duration, 1.f);
            time = phase * clip.duration;
        }
        else if (state == CharacterState::JumpStart || state == CharacterState::JumpLand)
            time = std::min(stateTime / (state == CharacterState::JumpStart ? StartSeconds : LandSeconds), 1.f) * clip.duration;
        else
            time = std::fmod(time, clip.duration);
        blendTime_ += dt;
        const auto target = EvaluateGltfLocalPose(*model_, Clip(), time);
        localPose = BlendGltfLocalPoses(sourcePose_, target, blendTime_ / BlendSeconds);
        const float follow = 1 - std::exp(-10 * dt);
        cameraTarget.x += (position.x - cameraTarget.x) * follow;
        cameraTarget.y += (position.y + 1 - cameraTarget.y) * follow;
        cameraTarget.z += (position.z - cameraTarget.z) * follow;
    }
    void UpdateCamera()
    {
        cameraEye = {cameraTarget.x + 4 * std::sin(cameraYaw) * std::cos(cameraPitch),
                     cameraTarget.y + 4 * std::sin(cameraPitch),
                     cameraTarget.z + 4 * std::cos(cameraYaw) * std::cos(cameraPitch)};
    }
};
} // namespace tutorial
