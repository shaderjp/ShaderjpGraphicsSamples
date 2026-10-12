#include "../LightingLab/Scene.hlsli"
VK_BINDING(1, 0) Texture2D<float4> monitorTexture : register(t0);
VK_BINDING(2, 0) SamplerState monitorSampler : register(s0);
float4 PSMain(Output i) : SV_Target
{
    return float4(monitorTexture.Sample(monitorSampler, i.uv).rgb, 1);
}
