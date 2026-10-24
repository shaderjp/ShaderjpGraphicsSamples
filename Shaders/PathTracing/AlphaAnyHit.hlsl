#include "Scene.hlsli"
[shader("anyhit")] void AlphaAnyHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attr) {
    if (AlphaRejected(attr.barycentrics))
        IgnoreHit();
}
