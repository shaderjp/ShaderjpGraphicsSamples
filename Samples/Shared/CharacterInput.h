#pragma once
#include "CharacterController.h"
#include <Xinput.h>
#include <imgui.h>
#include <windowsx.h>

namespace tutorial
{
inline DirectX::XMFLOAT2 CharacterStick(SHORT x, SHORT y, float deadzone)
{
    const float length = std::hypot(float(x), float(y));
    if (length <= deadzone) return {};
    const float magnitude = (std::min(length, 32767.f) - deadzone) / (32767.f - deadzone);
    return {x / length * magnitude, y / length * magnitude};
}
struct CharacterPadDecoder
{
    bool primed = false;
    WORD previous = 0;
    void Reset() { primed = false; previous = 0; }
    CharacterCommand Sample(const XINPUT_GAMEPAD &pad)
    {
        CharacterCommand c;
        c.move = CharacterStick(pad.sThumbLX, pad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        auto look = CharacterStick(pad.sThumbRX, pad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        c.lookVelocity = {-look.x * 2.1f, -look.y * 2.1f};
        c.run = (pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
        const WORD pressed = primed ? WORD(pad.wButtons & ~previous) : 0;
        c.jump = (pressed & XINPUT_GAMEPAD_A) != 0;
        c.reset = (pressed & XINPUT_GAMEPAD_BACK) != 0;
        previous = pad.wButtons;
        primed = true;
        return c;
    }
};
inline CharacterCommand CombineCharacterInput(const CharacterCommand &keyboard, CharacterCommand pad)
{
    if (keyboard.move.x != 0 || keyboard.move.y != 0)
    {
        pad.move = keyboard.move;
        pad.run = keyboard.run;
    }
    pad.jump |= keyboard.jump;
    pad.reset |= keyboard.reset;
    if (keyboard.orbitDelta.x != 0 || keyboard.orbitDelta.y != 0)
    {
        pad.orbitDelta = keyboard.orbitDelta;
        pad.lookVelocity = {};
    }
    return pad;
}
class CharacterInput
{
  public:
    DWORD selected = XUSER_MAX_COUNT;
    bool active = true;
    void Clear()
    {
        keys_.fill(false);
        jump_ = reset_ = false;
        dx_ = dy_ = 0;
        decoder_.Reset();
        StopDrag();
    }
    bool Message(HWND window, UINT message, WPARAM key, LPARAM point)
    {
        window_ = window;
        if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !key) ||
            (message == WM_SIZE && key == SIZE_MINIMIZED))
            Clear();
        if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED) StopDrag();
        if ((message == WM_KEYUP || message == WM_SYSKEYUP) && key < keys_.size()) keys_[key] = false;
        const auto &io = ImGui::GetIO();
        if (message == WM_KEYDOWN && key < keys_.size() && !io.WantCaptureKeyboard)
        {
            const bool fresh = (point & (LPARAM(1) << 30)) == 0;
            if (key == VK_SPACE && !keys_[key] && fresh) jump_ = true;
            if (key == 'R' && !keys_[key] && fresh) reset_ = true;
            keys_[key] = true;
        }
        if (message == WM_RBUTTONDOWN && !io.WantCaptureMouse)
        {
            dragging_ = true;
            last_ = {GET_X_LPARAM(point), GET_Y_LPARAM(point)};
            SetCapture(window);
            return true;
        }
        if (message == WM_MOUSEMOVE && dragging_)
        {
            POINT next{GET_X_LPARAM(point), GET_Y_LPARAM(point)};
            dx_ += float(next.x - last_.x);
            dy_ += float(next.y - last_.y);
            last_ = next;
            return true;
        }
        if (message == WM_RBUTTONUP && dragging_)
        {
            const float x = dx_, y = dy_;
            StopDrag();
            dx_ = x; dy_ = y;
            return true;
        }
        return false;
    }
    CharacterCommand ConsumeKeyboard()
    {
        CharacterCommand c;
        c.move = {float(keys_['D']) - float(keys_['A']), float(keys_['W']) - float(keys_['S'])};
        c.run = keys_[VK_SHIFT];
        c.jump = jump_; c.reset = reset_;
        c.orbitDelta = {-dx_ * .004f, dy_ * .004f};
        jump_ = reset_ = false;
        dx_ = dy_ = 0;
        return c;
    }
    CharacterCommand Poll(HWND window)
    {
        XINPUT_STATE state{};
        const ULONGLONG now = GetTickCount64();
        if (selected < XUSER_MAX_COUNT && XInputGetState(selected, &state) != ERROR_SUCCESS)
        {
            selected = XUSER_MAX_COUNT;
            decoder_.Reset();
            nextProbe_ = 0;
        }
        if (selected == XUSER_MAX_COUNT && now >= nextProbe_)
        {
            for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
                if (XInputGetState(i, &state) == ERROR_SUCCESS) { selected = i; break; }
            nextProbe_ = now + 2000;
        }
        const auto &io = ImGui::GetIO();
        active = GetForegroundWindow() == window && !IsIconic(window) && !io.WantCaptureKeyboard && !io.WantCaptureMouse;
        if (!active) { Clear(); return {}; }
        if (io.WantCaptureMouse) StopDrag();
        const auto keyboard = ConsumeKeyboard();
        auto pad = selected < XUSER_MAX_COUNT ? decoder_.Sample(state.Gamepad) : CharacterCommand{};
        if (io.WantCaptureMouse) pad.lookVelocity = {};
        auto result = CombineCharacterInput(keyboard, pad);
        if (result.reset) Clear();
        return result;
    }

  private:
    std::array<bool, 256> keys_{};
    CharacterPadDecoder decoder_;
    HWND window_ = nullptr;
    POINT last_{};
    bool dragging_ = false, jump_ = false, reset_ = false;
    float dx_ = 0, dy_ = 0;
    ULONGLONG nextProbe_ = 0;
    void StopDrag()
    {
        const bool owned = dragging_;
        dragging_ = false;
        dx_ = dy_ = 0;
        if (owned && window_ && GetCapture() == window_) ReleaseCapture();
    }
};
} // namespace tutorial
