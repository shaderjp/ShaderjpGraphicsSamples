#pragma once
#include "Image.h"
#include "MeshletData.h"
namespace tutorial
{
inline Image MeshTextureImage(const WorkshopImage &image)
{
    auto name = image.path.string();
    if (!name.starts_with("$mesh-"))
        return LoadWicImage(image.path);
    Image result;
    result.width = result.height = 2;
    result.rgba.resize(16, 255);
    for (int i = 0; i < 4; ++i)
    {
        auto *p = result.rgba.data() + i * 4;
        if (name == "$mesh-normal")
        {
            p[0] = p[1] = 128;
            p[2] = 255;
        }
        if (name == "$mesh-orm")
        {
            p[0] = 255;
            p[1] = 128;
            p[2] = 0;
        }
        if (name == "$mesh-black")
            p[0] = p[1] = p[2] = 0;
        if (name == "$mesh-mask")
            p[3] = i % 2 ? 255 : 64;
        if (name == "$mesh-uv")
        {
            p[0] = i == 0 || i == 3 ? 255 : 0;
            p[1] = i == 1 || i == 3 ? 255 : 0;
            p[2] = i >= 2 ? 255 : 0;
        }
    }
    return result;
}
inline Workshop MeshFixture(Workshop w, const std::wstring &name)
{
    if (name.empty())
        return w;
    const std::wstring names[] = {L"uv",    L"transform",   L"mask",     L"mask-back",
                                  L"empty", L"all-visible", L"backface", L"shadow"};
    if (std::find(std::begin(names), std::end(names), name) == std::end(names))
        throw std::runtime_error("Unknown --mesh-test-scene");
    using namespace DirectX;
    w.primitives.clear();
    w.materials.clear();
    w.images.clear();
    w.scenes.clear();
    w.cameras.clear();
    w.path.clear();
    w.images = {{{"$mesh-uv"}, ImageSemantic::Color, 0},
                {{"$mesh-normal"}, ImageSemantic::Normal, 0},
                {{"$mesh-orm"}, ImageSemantic::Linear, 0},
                {{"$mesh-black"}, ImageSemantic::Color, 0},
                {{"$mesh-mask"}, ImageSemantic::Color, .5f}};
    WorkshopMaterial m{};
    m.images = {0, 1, 2, 3};
    m.factors = {0, 1, 0, 1};
    m.doubleSided = name != L"backface";
    w.materials = {m, m};
    if (name == L"mask" || name == L"mask-back")
    {
        w.materials[0].images[0] = 4;
        w.materials[0].alpha = AlphaMode::Mask;
        w.materials[0].base = {.1f, .9f, .2f, 1};
    }
    WorkshopScene scene{};
    scene.name = "Standard";
    scene.minimum = {-10, -10, -10};
    scene.maximum = {10, 10, 10};
    scene.sun = {1, 1, 1, 2};
    XMStoreFloat3(&scene.direction, XMVector3Normalize(XMVectorSet(-1, 1, -2, 0)));
    auto plane = [&](uint32_t material, float size, XMMATRIX world, bool movable) {
        WorkshopPrimitive p{};
        p.material = material;
        p.minimum = {-size, -size, 0};
        p.maximum = {size, size, 0};
        p.vertices = {{{-size, -size, 0}, {0, 1}, {0, 0, -1}, {1, 0, 0, 1}},
                      {{-size, size, 0}, {0, 0}, {0, 0, -1}, {1, 0, 0, 1}},
                      {{size, size, 0}, {1, 0}, {0, 0, -1}, {1, 0, 0, 1}},
                      {{size, -size, 0}, {1, 1}, {0, 0, -1}, {1, 0, 0, 1}}};
        p.indices = {0, 1, 2, 0, 2, 3};
        WorkshopInstance i{};
        i.primitive = uint32_t(w.primitives.size());
        i.movable = movable;
        XMStoreFloat4x4(&i.world, world);
        w.primitives.push_back(p);
        scene.instances.push_back(i);
    };
    auto transform = name == L"transform"
                         ? XMMatrixScaling(.7f, .7f, .7f) * XMMatrixRotationZ(.2f) * XMMatrixTranslation(.5f, 0, 1)
                     : name == L"empty"    ? XMMatrixTranslation(100, 0, 0)
                     : name == L"backface" ? XMMatrixRotationY(XM_PI)
                                           : XMMatrixIdentity();
    plane(0, 2, transform, true);
    if (name == L"all-visible")
        plane(1, .2f, XMMatrixTranslation(.5f, .5f, -.5f), false);
    if (name == L"shadow")
        plane(1, .35f, XMMatrixTranslation(-1.3f, 1.3f, -1.5f), true);
    scene.name = "Standard";
    w.scenes.push_back(scene);
    scene.name = "Stress";
    w.scenes.push_back(scene);
    WorkshopCamera camera{};
    camera.name = "Hero";
    camera.position = {0, 0, name == L"mask-back" ? 4.f : -4.f};
    camera.target = {0, 0, 0};
    camera.fov = name == L"shadow" ? 28.f : 60.f;
    camera.nearPlane = .05f;
    camera.farPlane = 30;
    w.cameras.push_back(camera);
    camera.name = "StressHero";
    w.cameras.push_back(camera);
    w.path.assign(1201, w.cameras[0]);
    return w;
}
} // namespace tutorial
