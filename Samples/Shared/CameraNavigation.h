#pragma once
#include "Application.h"
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
#include <windowsx.h>

namespace tutorial
{
// CPU input and camera pose only; resource updates and projection remain in each sample.
class CameraNavigation
{
  public:
    bool test = false;
    void Reset()
    {
        manual_ = false;
        lensReady_ = false;
        keys_.fill(false);
        StopDrag();
    }
    bool Message(HWND window, UINT message, WPARAM key, LPARAM point)
    {
        window_ = window;
        if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !key))
        {
            keys_.fill(false);
            StopDrag();
        }
        if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE)
            StopDrag();
        const auto &io = ImGui::GetIO();
        if (message == WM_RBUTTONDOWN && !io.WantCaptureMouse)
        {
            dragging_ = true;
            last_ = {GET_X_LPARAM(point), GET_Y_LPARAM(point)};
            SetCapture(window);
            return true;
        }
        if (message == WM_RBUTTONUP && dragging_)
        {
            // A quick drag may press/move/release between two Update calls.
            // ReleaseCapture synchronously sends WM_CAPTURECHANGED, so preserve its last delta.
            const auto dx = dx_, dy = dy_;
            StopDrag();
            dx_ = dx;
            dy_ = dy;
            return true;
        }
        if (message == WM_MOUSEMOVE && dragging_)
        {
            POINT next{GET_X_LPARAM(point), GET_Y_LPARAM(point)};
            dx_ += next.x - last_.x;
            dy_ += next.y - last_.y;
            last_ = next;
            return true;
        }
        if (message == WM_KEYUP || message == WM_SYSKEYUP)
        {
            if (key < keys_.size())
                keys_[key] = false;
        }
        if (message == WM_KEYDOWN && !io.WantCaptureKeyboard && IsNavigationKey(key))
        {
            if (key == 'R')
                Reset();
            else
                keys_[key] = true;
            return true;
        }
        return false;
    }
    bool Update(HWND window, float seconds, const DirectX::XMFLOAT3 &position,
                const DirectX::XMFLOAT3 &target, bool locked = false)
    {
        using namespace DirectX;
        window_ = window;
        float dt = test ? 1.f / 60 : std::clamp(seconds - lastSeconds_, 0.f, .05f);
        lastSeconds_ = seconds;
        if (locked || (!test && GetForegroundWindow() != window) || ImGui::GetIO().WantCaptureKeyboard)
            keys_.fill(false);
        if (locked || (!test && GetForegroundWindow() != window) || ImGui::GetIO().WantCaptureMouse)
            StopDrag();
        float forward = float(keys_['W']) - float(keys_['S']);
        float right = float(keys_['D']) - float(keys_['A']);
        float vertical = float(keys_['E']) - float(keys_['Q']);
        bool input = forward != 0 || right != 0 || vertical != 0 || dx_ != 0 || dy_ != 0;
        if (!input)
            return false;
        if (!manual_)
        {
            eye_ = position;
            auto dir = XMVector3Normalize(XMLoadFloat3(&target) - XMLoadFloat3(&position));
            yaw_ = std::atan2(XMVectorGetX(dir), XMVectorGetZ(dir));
            pitch_ = std::asin(std::clamp(XMVectorGetY(dir), -1.f, 1.f));
            manual_ = true;
        }
        yaw_ = std::remainder(yaw_ + dx_ * .003f, XM_2PI);
        pitch_ = std::clamp(pitch_ - dy_ * .003f, -XM_PIDIV2 + .01f, XM_PIDIV2 - .01f);
        dx_ = dy_ = 0;
        auto look = XMVectorSet(std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_),
                                std::cos(yaw_) * std::cos(pitch_), 0);
        auto side = XMVectorSet(std::cos(yaw_), 0, -std::sin(yaw_), 0);
        auto move = look * forward + side * right + XMVectorSet(0, vertical, 0, 0);
        if (XMVectorGetX(XMVector3LengthSq(move)) > 0)
            move = XMVector3Normalize(move) * (keys_[VK_SHIFT] ? 8.f : 2.f) * dt;
        XMStoreFloat3(&eye_, XMLoadFloat3(&eye_) + move);
        XMStoreFloat3(&target_, XMLoadFloat3(&eye_) + look);
        return true;
    }
    const DirectX::XMFLOAT3 *Position() const
    {
        return manual_ ? &eye_ : nullptr;
    }
    const DirectX::XMFLOAT3 *Target() const
    {
        return manual_ ? &target_ : nullptr;
    }
    template <typename Camera>
    bool Update(HWND window, float seconds, const Camera &camera, bool locked = false)
    {
        const bool input = Update(window, seconds, camera.position, camera.target, locked);
        if (input && !lensReady_)
        {
            lens_ = {camera.fov, camera.nearPlane, camera.farPlane};
            lensReady_ = true;
        }
        return input;
    }
    const DirectX::XMFLOAT3 *Projection() const
    {
        return manual_ && lensReady_ ? &lens_ : nullptr;
    }
    void Hint() const
    {
        ImGui::TextWrapped("WASD: move | Q/E: down/up | Shift: fast | Right drag: look | R: reset camera");
    }

  private:
    static bool IsNavigationKey(WPARAM key)
    {
        return key == 'W' || key == 'A' || key == 'S' || key == 'D' || key == 'Q' || key == 'E' ||
               key == 'R' || key == VK_SHIFT;
    }
    void StopDrag()
    {
        dragging_ = false;
        dx_ = dy_ = 0;
        if (window_ && GetCapture() == window_)
            ReleaseCapture();
    }
    HWND window_ = nullptr;
    std::array<bool, 256> keys_{};
    bool dragging_ = false, manual_ = false, lensReady_ = false;
    POINT last_{};
    float dx_ = 0, dy_ = 0, yaw_ = 0, pitch_ = 0, lastSeconds_ = 0;
    DirectX::XMFLOAT3 eye_{}, target_{}, lens_{};
};
} // namespace tutorial
