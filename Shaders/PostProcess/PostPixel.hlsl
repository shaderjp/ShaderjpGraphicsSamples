#include "Post.hlsli"
VK_BINDING(1, 0) Texture2D<float4> sceneTexture : register(t0);
VK_BINDING(2, 0) SamplerState linearSampler : register(s0);
VK_BINDING(3, 0) Texture2D<float4> bloomTexture : register(t1);
float4 PSMain(Fullscreen i) : SV_Target
{
    float2 uv = i.uv;
    if (settings.w == 5)
    {
        float aspect = float(dimensions.x) / float(dimensions.y);
        uv = (uv - .5) * float2(max(aspect, 1), max(1 / aspect, 1)) + .5;
        if (any(uv < 0) || any(uv > 1))
            return float4(0, 0, 0, 1);
    }
    float3 c = sceneTexture.SampleLevel(linearSampler, uv, 0).rgb;
    if (settings.w < 2 && flags.x != 0)
        c += bloomTexture.SampleLevel(linearSampler, i.uv, 0).rgb * settings.z;
    if (settings.w == 1)
        c = dot(c, float3(.2126, .7152, .0722)).xxx;
    c = max(c * settings.x, 0);
    return float4(c / (1 + c), 1);
}
