#include "../Samples/Shared/CullingData.h"
#include <iostream>
#include <stdexcept>
using namespace tutorial;
using namespace DirectX;
void Require(bool result, const char *message)
{
    if (!result)
        throw std::runtime_error(message);
}
int main(int argc, char **argv)
{
    try
    {
        XMFLOAT4X4 vp;
        XMStoreFloat4x4(&vp, XMMatrixPerspectiveFovLH(XM_PIDIV2, 1, 1, 10));
        auto planes = CullPlanes(vp);
        auto box = [](float x, float y, float z, float e = .1f)
        { return CullBounds{{x - e, y - e, z - e, 0}, {x + e, y + e, z + e, 0}}; };
        Require(CullFrustum(box(0, 0, 3), planes) == 2, "inside");
        for (auto b :
             {box(-5, 0, 2), box(5, 0, 2), box(0, -5, 2), box(0, 5, 2), box(0, 0, .5f), box(0, 0, 11)})
            Require(CullFrustum(b, planes) == 0, "outside plane");
        Require(CullFrustum(box(0, 0, 1), planes) == 1, "near intersection");
        for (auto b : {box(-3, 0, 3), box(3, 0, 3), box(0, -3, 3), box(0, 3, 3), box(0, 0, 10)})
            Require(CullFrustum(b, planes) == 1, "intersecting each side and far plane");
        auto uncertain = box(0, 0, 3);
        uncertain.minimum.x = std::numeric_limits<float>::quiet_NaN();
        Require(!ProjectCullBounds(uncertain, vp, 1279, 719).safe, "nonfinite projection remains visible");
        Require(!ProjectCullBounds(box(0, 0, 0), vp, 1279, 719).safe, "camera inside must stay visible");
        Require(!ProjectCullBounds(box(0, 0, 1), vp, 1279, 719).safe, "near intersection must stay visible");
        auto projected = ProjectCullBounds(box(0, 0, 3), vp, 1279, 719);
        Require(projected.safe, "ordinary projection");
        Require(projected.left < 639.5f && projected.right > 639.5f, "rectangle covers center");
        SoftwareDepth d;
        d.Reset(16, 16);
        d.Triangle({-1, -1, .2f, 1}, {1, -1, .2f, 1}, {-1, 1, .2f, 1});
        Require(std::all_of(d.pixels.begin(), d.pixels.end(), [](float z) { return z == 1; }),
                "back faces must not occlude");
        d.Triangle({-1, -1, .2f, 1}, {-1, 1, .2f, 1}, {1, -1, .2f, 1});
        Require(d.pixels[12 * 16 + 2] < .21f, "fully covered cell written");
        Require(d.pixels[2 * 16 + 12] == 1, "uncovered cell stays far");
        CullProjection hidden{2, 11, 4, 13, .8f, true};
        Require(d.Hidden(hidden, 16, 16), "covered object hidden");
        hidden.nearest = .1f;
        Require(!d.Hidden(hidden, 16, 16), "near object visible");
        hidden = {2, 2, 14, 14, .8f, true};
        Require(!d.Hidden(hidden, 16, 16), "partial coverage visible");
        hidden.safe = false;
        Require(!d.Hidden(hidden, 16, 16), "uncertain projection visible");
        d.Reset(17, 9);
        d.Triangle({-1, -1, -.2f, 1}, {1, -1, .5f, 1}, {0, 1, .5f, 1});
        for (float depth : d.pixels)
            Require(std::isfinite(depth) && depth >= 0 && depth <= 1, "clipped depth finite");
        Require(CullPowerOfTwo(1279) == 2048 && CullPowerOfTwo(719) == 1024, "odd size padding");
        if (argc > 1)
        {
            auto scene =
                LoadCullingLab(std::filesystem::path(argv[1]) / "Assets/Scenes/CullingLab/CullingLab.json");
            for (const auto &s : scene.scenes)
            {
                size_t targets = 0, doors = 0;
                for (const auto &o : s.instances)
                {
                    targets += o.id.starts_with("object:");
                    doors += o.movable;
                }
                Require(targets == (s.name == "Mini"       ? 64
                                    : s.name == "Standard" ? 512
                                                           : 4096),
                        "exact scene target count");
                Require(doors == 1, "one moving door");
            }
            Require(scene.path.size() == 1201, "fixed camera path");
        }
        std::cout << "Culling CPU tests passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
