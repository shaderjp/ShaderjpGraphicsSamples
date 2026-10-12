#include "Post.hlsli"
VK_BINDING(1, 0) Texture2D<float4> inputTexture : register(t0);
#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
VK_BINDING(4, 0) RWTexture2D<float4> outputTexture : register(u0);
[numthreads(8, 8, 1)] void CSMain(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= dimensions.zw))
        return;
    const float weights[5] = {.227027027, .194594595, .121621622, .054054054, .016216216};
    int2 axis = flags.y == 1 ? int2(1, 0) : int2(0, 1), p = int2(id.xy);
    float3 c = 0;
    for (int x = -4; x <= 4; ++x)
        c +=
            inputTexture.Load(int3(clamp(p + axis * x, 0, int2(dimensions.zw) - 1), 0)).rgb * weights[abs(x)];
    outputTexture[id.xy] = float4(c, 1);
}
