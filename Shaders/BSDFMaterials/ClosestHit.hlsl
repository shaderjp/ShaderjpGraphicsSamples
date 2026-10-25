#include "Scene.hlsli"
[shader("closesthit")] void ClosestHit(inout Payload p, in BuiltInTriangleIntersectionAttributes a)
{
    p.bary = a.barycentrics; p.distance = RayTCurrent();
    p.instance = InstanceID(); p.triIndex = PrimitiveIndex(); p.kind = HitKind();
}
