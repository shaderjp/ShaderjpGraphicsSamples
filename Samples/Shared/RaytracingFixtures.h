#pragma once
#include "RaytracingData.h"
namespace tutorial
{
// Small CPU-authored scenes give the GPU tests independent, analytically known inputs.
inline Image RayTextureImage(const WorkshopImage &i)
{
    auto name = i.path.string();
    if (!name.starts_with("$rt-"))
        return LoadWicImage(i.path);
    Image im;
    im.width = im.height = 2;
    im.rgba.resize(16, 255);
    for (int p = 0; p < 4; ++p)
    {
        auto *c = im.rgba.data() + p * 4;
        if (name == "$rt-normal")
        {
            c[0] = c[1] = 128;
            c[2] = 255;
        }
        if (name == "$rt-orm")
        {
            c[0] = 255;
            c[1] = 128;
            c[2] = 0;
        }
        if (name == "$rt-black")
            c[0] = c[1] = c[2] = 0;
        if (name == "$rt-mask")
            c[3] = (p % 2) ? 255 : 64;
        if (name == "$rt-uv")
        {
            c[0] = (p == 0 || p == 3) ? 255 : 0;
            c[1] = (p == 1 || p == 3) ? 255 : 0;
            c[2] = p >= 2 ? 255 : 0;
        }
    }
    return im;
}
inline Workshop RayFixture(Workshop w, const std::wstring &fixture)
{
    if (fixture.empty())
        return w;
    using namespace DirectX;
    const std::wstring cases[] = {L"albedo",
                                  L"transform",
                                  L"mask",
                                  L"mask-back",
                                  L"sun-shadow",
                                  L"point-shadow",
                                  L"point-beyond",
                                  L"mask-shadow-hole",
                                  L"mask-shadow-solid",
                                  L"reflection",
                                  L"reflection-moved",
                                  L"reflection-shadow",
                                  L"reflection-mask-hole",
                                  L"reflection-mask-solid",
                                  L"environment"};
    if (std::find(std::begin(cases), std::end(cases), fixture) == std::end(cases))
        throw std::runtime_error("Unknown --rt-test-scene");
    w.primitives.clear();
    w.materials.clear();
    w.images.clear();
    w.scenes.clear();
    w.cameras.clear();
    w.path.clear();
    w.images = {{{"$rt-white"}, ImageSemantic::Color, 0},  {{"$rt-normal"}, ImageSemantic::Normal, 0},
                {{"$rt-orm"}, ImageSemantic::Linear, 0},   {{"$rt-white"}, ImageSemantic::Color, 0},
                {{"$rt-mask"}, ImageSemantic::Color, .5f}, {{"$rt-uv"}, ImageSemantic::Color, 0}};
    WorkshopMaterial material;
    material.id = "fixture";
    material.images = {0, 1, 2, 3};
    material.base = {.8f, .4f, .2f, 1};
    material.factors = {0, 1, 0, 1};
    material.emission = {0, 0, 0, 0};
    material.doubleSided = true;
    w.materials = {material, material, material};
    WorkshopScene s;
    s.name = "Standard";
    s.minimum = {-5, -5, -5};
    s.maximum = {5, 5, 5};
    s.sun = {0, 0, 0, 0};
    XMStoreFloat3(&s.direction, XMVector3Normalize(XMVectorSet(-1, 1, -2, 0)));
    auto plane = [&](uint32_t mat, float size, XMMATRIX world, bool movable = false) {
        WorkshopPrimitive p;
        p.id = "plane";
        p.material = mat;
        p.minimum = {-size, -size, 0};
        p.maximum = {size, size, 0};
        p.vertices = {{{-size, -size, 0}, {0, 1}, {0, 0, -1}, {1, 0, 0, 1}},
                      {{-size, size, 0}, {0, 0}, {0, 0, -1}, {1, 0, 0, 1}},
                      {{size, size, 0}, {1, 0}, {0, 0, -1}, {1, 0, 0, 1}},
                      {{size, -size, 0}, {1, 1}, {0, 0, -1}, {1, 0, 0, 1}}};
        p.indices = {0, 1, 2, 0, 2, 3};
        WorkshopInstance i;
        i.id = "instance";
        i.primitive = uint32_t(w.primitives.size());
        i.movable = movable;
        XMStoreFloat4x4(&i.world, world);
        w.primitives.push_back(p);
        s.instances.push_back(i);
    };
    WorkshopCamera cam;
    cam.name = "Hero";
    cam.position = {0, 0, -4};
    cam.target = {0, 0, 0};
    cam.fov = 60;
    cam.nearPlane = .05f;
    cam.farPlane = 30;
    if (fixture == L"albedo" || fixture == L"transform")
    {
        w.materials[0].images[0] = 5;
        w.materials[0].base = {1, 1, 1, 1};
        plane(0, 2,
              fixture == L"transform"
                  ? XMMatrixScaling(.7f, .7f, .7f) * XMMatrixRotationZ(.2f) * XMMatrixTranslation(.5f, 0, 1)
                  : XMMatrixIdentity());
    }
    else if (fixture == L"mask" || fixture == L"mask-back")
    {
        w.materials[0].images[0] = 4;
        w.materials[0].base = {.1f, .9f, .2f, 1};
        w.materials[0].alpha = AlphaMode::Mask;
        w.materials[1].base = {.9f, .1f, .2f, 1};
        plane(0, 2, XMMatrixIdentity());
        plane(1, 2, XMMatrixTranslation(0, 0, fixture == L"mask-back" ? -1.f : 1.f));
        if (fixture == L"mask-back")
            cam.position = {0, 0, 4};
    }
    else if (fixture == L"sun-shadow" || fixture == L"point-shadow" || fixture == L"point-beyond" ||
             fixture == L"mask-shadow-hole" || fixture == L"mask-shadow-solid")
    {
        plane(0, 2, XMMatrixIdentity());
        w.materials[1].base = {.2f, .2f, .2f, 1};
        plane(1, .3f,
              fixture == L"point-beyond" ? XMMatrixTranslation(-1.5f, 1.5f, -3) : XMMatrixTranslation(-.5f, .5f, -1),
              true);
        if (fixture == L"mask-shadow-hole" || fixture == L"mask-shadow-solid")
        {
            w.materials[1].alpha = AlphaMode::Mask;
            w.materials[1].images[0] = 4;
            XMStoreFloat4x4(&s.instances.back().world,
                            XMMatrixTranslation(fixture == L"mask-shadow-hole" ? -.4f : -.6f, .5f, -1));
        }
        if (fixture == L"sun-shadow")
            s.sun = {1, 1, 1, 1};
        else
            s.lights.push_back({{-1, 1, -2, 0}, {1, 1, 1, 4}});
    }
    else
    {
        cam.fov = 20;
        w.materials[0].base = {1, 1, 1, 1};
        w.materials[0].factors = {1, 1, 0, 1};
        w.images[2].path = "$rt-white";
        plane(0, 2, XMMatrixRotationY(-XM_PIDIV4));
        if (fixture != L"environment")
        {
            w.materials[1].base = {0, 0, 0, 1};
            w.materials[1].emission = {.25f, .5f, 1, 0};
            w.materials[1].factors = {0, 1, 0, 1};
            if (fixture == L"reflection-shadow")
            {
                w.materials[1].base = {.8f, .4f, .2f, 1};
                w.materials[1].emission = {0, 0, 0, 0};
                s.lights.push_back({{1, 1, -1, 0}, {1, 1, 1, 8}});
            }
            if (fixture == L"reflection-mask-hole" || fixture == L"reflection-mask-solid")
            {
                w.materials[1].alpha = AlphaMode::Mask;
                w.materials[1].images[0] = 4;
                w.materials[1].base.w = fixture == L"reflection-mask-hole" ? .4f : 1.f;
            }
            plane(1, .4f,
                  XMMatrixRotationY(XM_PIDIV2) * XMMatrixTranslation(2, fixture == L"reflection-moved" ? 1.f : 0.f, 0),
                  true);
            if (fixture == L"reflection-shadow")
                plane(2, .2f, XMMatrixTranslation(1.5f, .5f, -.5f));
        }
    }
    w.scenes = {s};
    auto stress = s;
    stress.name = "Stress";
    w.scenes.push_back(stress);
    w.cameras = {cam};
    cam.name = "StressHero";
    w.cameras.push_back(cam);
    w.path.assign(1201, w.cameras[0]);
    return w;
}
} // namespace tutorial
