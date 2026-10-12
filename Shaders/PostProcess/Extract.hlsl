#include "Post.hlsli"
VK_BINDING(1, 0) Texture2D<float4> inputTexture : register(t0);
#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
VK_BINDING(4, 0) RWTexture2D<float4> outputTexture : register(u0);
[numthreads(8, 8, 1)] void CSMain(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= dimensions.zw))
        return;
    float3 c = 0;
    for (uint y = 0; y < 2; ++y)
        for (uint x = 0; x < 2; ++x)
            c += inputTexture.Load(int3(min(id.xy * 2 + uint2(x, y), dimensions.xy - 1), 0)).rgb * .25;
    float bright = max(c.r, max(c.g, c.b));
    c *= max(bright - settings.y, 0) / max(bright, 1e-6);
    outputTexture[id.xy] = float4(c, 1);
}
