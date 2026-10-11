#include "Ui.hlsli"
VK_BINDING(1, 0) Texture2D<float4> fontAtlas : register(t0);
VK_BINDING(2, 0) SamplerState fontSampler : register(s0);
float4 PSMain(UiOutput input) : SV_Target
{
    float4 color = input.color * fontAtlas.Sample(fontSampler, input.uv);
    // ImGui style colors are sRGB. The sRGB render target expects linear shader output.
    color.rgb = select(color.rgb <= 0.04045, color.rgb / 12.92, pow((color.rgb + 0.055) / 1.055, 2.4));
    return color;
}
