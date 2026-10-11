#include "../Common.hlsli"
VK_BINDING(1, 0) Texture2D<float4> colorTexture : register(t0);
VK_BINDING(2, 0) SamplerState colorSampler : register(s0);
float4 PSMain(TextureOutput input) : SV_Target { return colorTexture.Sample(colorSampler, input.uv); }
