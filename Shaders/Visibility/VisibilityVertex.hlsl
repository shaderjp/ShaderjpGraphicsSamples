#include "../Deferred/Scene.hlsli"
#include "VisibilityRaster.hlsli"
VisibilityVertex VSMain(Input i)
{
    VisibilityVertex o;
    o.position = mul(mul(float4(i.position,1), world), viewProjection);
    o.uv = i.uv;
    return o;
}
