#pragma once
#include "CameraNavigation.h"
#include "Gltf.h"
#include "Image.h"
namespace tutorial
{
struct alignas(16) BasicMeshVertex
{
    DirectX::XMFLOAT4 positionU, normalV;
};
struct alignas(16) BasicMeshConstants
{
    DirectX::XMFLOAT4X4 mvp;
    DirectX::XMFLOAT4 base;
    DirectX::XMUINT4 counts;
};
static_assert(sizeof(BasicMeshVertex) == 32 && sizeof(BasicMeshConstants) == 96);
struct BasicMeshPanel
{
    CameraNavigation navigation;
    bool visible = true, vertex = false, test = false, path = false, play = false;
    int pathFrame = 0;
    float offset = 0;
    uint32_t frame = 0;
    DirectX::XMFLOAT3 eye{}, target{};
    float fov = 45, nearPlane = .01f, farPlane = 100;
    ImVec2 editPoint{}, resetPoint{};
    std::filesystem::path screenshot;
    void Initialize(const GltfModel &m, const Options &o)
    {
        using namespace DirectX;
        visible = !o.hideUi;
        vertex = o.meshRenderPath == L"vertex";
        test = o.testUi;
        screenshot = o.screenshot;
        navigation.test = o.testCamera;
        if (!o.meshRenderPath.empty() && o.meshRenderPath != L"vertex" && o.meshRenderPath != L"mesh")
            throw std::runtime_error("Basic Mesh Shader supports --render-path vertex / mesh");
        if (o.preset == L"Full" || o.workshopScene == L"Stress")
            throw std::runtime_error("Basic Mesh Shader uses a single Opaque ShaderBall");
        if (!o.meshCulling.empty() && o.meshCulling != L"off")
            throw std::runtime_error("Basic Mesh Shader has no meshlet culling");
        if (!o.meshView.empty() && o.meshView != L"off")
            throw std::runtime_error("Basic Mesh Shader has no meshlet visualization");
        auto lo = XMLoadFloat3(&m.minimum), hi = XMLoadFloat3(&m.maximum);
        auto center = (lo + hi) * .5f;
        XMStoreFloat3(&target, center);
        float r =
            std::max({m.maximum.x - m.minimum.x, m.maximum.y - m.minimum.y, m.maximum.z - m.minimum.z, .002f}) * .5f;
        XMStoreFloat3(&eye, center + XMVector3Normalize(XMVectorSet(-.82f, .32f, .56f, 0)) * r * 3.8f);
        nearPlane = std::max(.001f, r * .038f);
        farPlane = r * 14;
        if (!o.meshTestScene.empty())
        {
            eye = {0, 0, -4};
            target = {0, 0, 0};
            fov = 60;
            nearPlane = .05f;
            farPlane = 30;
        }
        path = o.cameraPath;
        play = path && !o.frames;
        pathFrame = int(o.pathFrame);
        if (test && (!visible || o.frames < 36))
            throw std::runtime_error("--test-ui requires visible UI and at least 36 frames");
    }
    void Reset()
    {
        navigation.Reset();
        offset = 0;
        vertex = false;
        path = play = false;
        pathFrame = 0;
    }
    void QueueTestInput()
    {
        ++frame;
        if (!test)
            return;
        auto &io = ImGui::GetIO();
        io.AddFocusEvent(true);
        if (frame == 8 || frame == 18)
        {
            auto p = frame == 8 ? editPoint : resetPoint;
            io.AddMousePosEvent(p.x, p.y);
            io.AddMouseButtonEvent(0, true);
        }
        if (frame == 9 || frame == 19)
            io.AddMouseButtonEvent(0, false);
    }
    std::filesystem::path TestCapture() const
    {
        const wchar_t *s = frame == 6 ? L"-baseline" : frame == 14 ? L"-edited" : frame == 24 ? L"-reset" : nullptr;
        return test && s && !screenshot.empty() ? screenshot.parent_path() / (screenshot.stem().wstring() + s + L".png")
                                                : std::filesystem::path{};
    }
    DirectX::XMFLOAT3 Eye() const
    {
        using namespace DirectX;
        if (!path)
            return eye;
        XMFLOAT3 p;
        auto delta = XMLoadFloat3(&eye) - XMLoadFloat3(&target);
        XMStoreFloat3(&p, XMLoadFloat3(&target) +
                              XMVector3TransformNormal(delta, XMMatrixRotationY(XM_2PI * pathFrame / 1200.f)));
        return p;
    }
    void Draw()
    {
        if (!visible)
            return;
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({330, 250}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Mesh Shader Basic");
        ImGui::Text("Advanced 04 / ShaderBall Albedo");
        ImGui::Checkbox("Vertex Shader reference", &vertex);
        ImGui::SliderFloat("Model X", &offset, -1.5f, 1.5f);
        auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        editPoint = {a.x + 175, (a.y + b.y) * .5f};
        if (ImGui::Checkbox("Camera path", &path))
            navigation.Reset();
        ImGui::SameLine();
        ImGui::Checkbox("Play", &play);
        ImGui::SliderInt("Path frame", &pathFrame, 0, 1200);
        if (ImGui::Button("Reset"))
            Reset();
        a = ImGui::GetItemRectMin();
        b = ImGui::GetItemRectMax();
        resetPoint = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
        navigation.Hint();
        ImGui::End();
    }
};
inline BasicMeshConstants MakeBasicMeshConstants(const GltfModel &m, const BasicMeshPanel &panel, uint32_t width,
                                                 uint32_t height)
{
    using namespace DirectX;
    auto eye = panel.Eye();
    auto vp =
        XMMatrixLookAtLH(XMLoadFloat3(panel.navigation.Position() ? panel.navigation.Position() : &eye),
                         XMLoadFloat3(panel.navigation.Target() ? panel.navigation.Target() : &panel.target),
                         XMVectorSet(0, 1, 0, 0)) *
        XMMatrixPerspectiveFovLH(XMConvertToRadians(panel.fov), float(width) / height, panel.nearPlane, panel.farPlane);
    BasicMeshConstants c{};
    XMStoreFloat4x4(&c.mvp, XMMatrixTranslation(panel.offset, 0, 0) * vp);
    c.base = m.baseColorFactor;
    c.counts = {uint32_t(m.mesh.indices.size()), 0, 0, 0};
    return c;
}
inline GltfModel LoadBasicMesh(const Options &o)
{
    if (o.meshTestScene.empty())
        return LoadGltfAlbedo(o.model.empty() ? AssetPath(L"Assets/Models/ShaderBall/ShaderBall_Albedo.gltf")
                                              : o.model);
    if (o.meshTestScene != L"uv" && o.meshTestScene != L"transform")
        throw std::runtime_error("Basic --mesh-test-scene: uv / transform");
    using namespace DirectX;
    GltfModel m{};
    m.albedoPath = "$basic-uv";
    m.minimum = {-2, -2, 0};
    m.maximum = {2, 2, 0};
    m.mesh.vertices = {{{-2, -2, 0}, {0, 1}, {0, 0, -1}},
                       {{-2, 2, 0}, {0, 0}, {0, 0, -1}},
                       {{2, 2, 0}, {1, 0}, {0, 0, -1}},
                       {{2, -2, 0}, {1, 1}, {0, 0, -1}}};
    m.mesh.indices = {0, 1, 2, 0, 2, 3};
    if (o.meshTestScene == L"transform")
    {
        auto transform = XMMatrixScaling(.7f, .7f, .7f) * XMMatrixRotationZ(.2f) * XMMatrixTranslation(.5f, 0, 1);
        for (auto &v : m.mesh.vertices)
            XMStoreFloat3(&v.position, XMVector3TransformCoord(XMLoadFloat3(&v.position), transform));
    }
    return m;
}
inline Image BasicMeshTexture(const std::filesystem::path &path)
{
    if (path != "$basic-uv")
        return LoadWicImage(path);
    Image im;
    im.width = im.height = 2;
    im.rgba = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    return im;
}
} // namespace tutorial
