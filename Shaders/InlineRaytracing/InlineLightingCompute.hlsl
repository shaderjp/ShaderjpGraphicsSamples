#define INLINE_RT 1
#include "../Deferred/DeferredRead.hlsli"
#if INLINE_SAMPLE == 27
#include "Reflection.hlsli"
#endif
#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
VK_BINDING(18, 0) RWTexture2D<float4> hdrOutput : register(u0);
[numthreads(8, 8, 1)] void CSMain(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= uint2(screen.xy)))
        return;
    float3 color = ShadePixel(id.xy);
    if (inlineControls.w == 2)
        color = float3(inlineRayCount / 32.0, inlineRayCount / 128.0, 0);
    hdrOutput[id.xy] = float4(color, 1);
}
