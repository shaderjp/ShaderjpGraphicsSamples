#include "../Samples/Shared/CameraNavigation.h"
#include <iostream>
#include <stdexcept>
using namespace tutorial;
using namespace DirectX;
namespace
{
void Require(bool b, const char *message)
{
    if (!b)
        throw std::runtime_error(message);
}
float Distance(const XMFLOAT3 &a, const XMFLOAT3 &b)
{
    return XMVectorGetX(XMVector3Length(XMLoadFloat3(&a) - XMLoadFloat3(&b)));
}
} // namespace
int main()
{
    try
    {
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        const XMFLOAT3 eye{0, 0, 0}, target{0, 0, 1};
        CameraNavigation c;
        c.test = true;
        c.Update(nullptr, 0, eye, target);
        Require(!c.Position(), "Idle preserves original pose");
        c.Message(nullptr, WM_KEYDOWN, 'W', 0);
        c.Update(nullptr, 1, eye, target);
        Require(c.Position()->z > 0 && std::abs(c.Position()->x) < 1e-6f, "W follows LH forward");
        float normal = Distance(*c.Position(), eye);
        c.Reset();
        c.Message(nullptr, WM_KEYDOWN, 'W', 0);
        c.Message(nullptr, WM_KEYDOWN, 'D', 0);
        c.Update(nullptr, 2, eye, target);
        Require(std::abs(Distance(*c.Position(), eye) - normal) < 1e-6f, "Diagonal normalized");
        c.Reset();
        c.Message(nullptr, WM_KEYDOWN, 'E', 0);
        c.Message(nullptr, WM_KEYDOWN, VK_SHIFT, 0);
        c.Update(nullptr, 3, eye, target);
        Require(std::abs(c.Position()->y - normal * 4) < 1e-6f, "Shift accelerates E vertically");
        auto stopped = *c.Position();
        c.Message(nullptr, WM_KILLFOCUS, 0, 0);
        c.Update(nullptr, 4, eye, target);
        Require(Distance(stopped, *c.Position()) == 0, "Focus loss releases keys");
        c.Reset();
        io.WantCaptureKeyboard = true;
        c.Message(nullptr, WM_KEYDOWN, 'W', 0);
        c.Update(nullptr, 5, eye, target);
        Require(!c.Position(), "UI keyboard capture blocks movement");
        io.WantCaptureKeyboard = false;
        c.Message(nullptr, WM_KEYDOWN, 'W', 0);
        c.Update(nullptr, 6, eye, target, true);
        Require(!c.Position(), "Benchmark lock blocks movement");
        c.Message(nullptr, WM_RBUTTONDOWN, 0, MAKELPARAM(0, 0));
        c.Message(nullptr, WM_MOUSEMOVE, MK_RBUTTON, MAKELPARAM(200, 30000));
        c.Update(nullptr, 7, eye, target);
        auto dir = XMVector3Normalize(XMLoadFloat3(c.Target()) - XMLoadFloat3(c.Position()));
        Require(std::isfinite(XMVectorGetY(dir)) && std::abs(XMVectorGetY(dir)) < 1,
                "Pitch clamp avoids pole");
        Require(XMVectorGetX(dir) > 0, "Right drag turns right");
        auto direction = *c.Target();
        c.Message(nullptr, WM_CAPTURECHANGED, 0, 0);
        c.Message(nullptr, WM_MOUSEMOVE, MK_RBUTTON, MAKELPARAM(-100, -100));
        c.Update(nullptr, 8, eye, target);
        Require(Distance(direction, *c.Target()) == 0, "Capture loss stops rotation");
        c.Message(nullptr, WM_KEYDOWN, 'R', 0);
        Require(!c.Position() && !c.Target(), "R restores exact original path");
        c.Message(nullptr, WM_RBUTTONDOWN, 0, MAKELPARAM(0, 0));
        c.Message(nullptr, WM_MOUSEMOVE, MK_RBUTTON, MAKELPARAM(40, 20));
        c.Message(nullptr, WM_RBUTTONUP, 0, MAKELPARAM(40, 20));
        c.Update(nullptr, 9, eye, target);
        Require(c.Position() && c.Target()->x > 0, "Quick drag retains movement on normal release");
        c.Reset();
        io.WantCaptureMouse = true;
        c.Message(nullptr, WM_RBUTTONDOWN, 0, MAKELPARAM(0, 0));
        c.Message(nullptr, WM_MOUSEMOVE, MK_RBUTTON, MAKELPARAM(100, 100));
        c.Update(nullptr, 10, eye, target);
        Require(!c.Position(), "UI mouse capture blocks rotation");
        io.WantCaptureMouse = false;
        struct Camera
        {
            XMFLOAT3 position{}, target{0, 0, 1};
            float fov = 50, nearPlane = .05f, farPlane = 150;
        } path;
        c.Message(nullptr, WM_KEYDOWN, 'W', 0);
        c.Update(nullptr, 11, path);
        path.fov = 40;
        path.nearPlane = .2f;
        path.farPlane = 300;
        c.Update(nullptr, 12, path);
        Require(c.Projection() && c.Projection()->x == 50 && c.Projection()->y == .05f &&
                    c.Projection()->z == 150,
                "Path takeover preserves FOV, near and far");
        c.Reset();
        Require(!c.Projection(), "Reset restores authored projection");
        ImGui::DestroyContext();
        std::cout << "Camera navigation CPU tests passed (basis, speed, UI, focus, capture, pitch, Reset)\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
