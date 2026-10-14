#include "../Deferred/Resources.hlsli"
#include "VisibilityRaster.hlsli"
// Zero is background. Bit 31 of primitiveId ID records the rasterizer's back face.
uint2 PSMain(VisibilityVertex i, uint primitiveId : SV_PrimitiveID, bool front : SV_IsFrontFace) : SV_Target0
{
    // OPAQUE evaluates no textures; MASK uses exactly the raster material's alpha test.
    if (emissiveFactor.w == 1)
        clip(baseTexture.Sample(materialSampler, i.uv).a * baseColorFactor.a - screen.w);
    return uint2(uint(screen.z), primitiveId | (front ? 0 : 0x80000000));
}
