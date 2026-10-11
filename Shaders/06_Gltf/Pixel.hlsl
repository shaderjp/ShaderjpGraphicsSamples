#include "Scene.hlsli"
VK_BINDING(1, 0) Texture2D<float4> albedoTexture : register(t0);
VK_BINDING(2, 0) SamplerState albedoSampler : register(s0);
float4 PSMain(TextureOutput input) : SV_Target
{
    // sRGB view decodes to linear; OPAQUE materials ignore image/factor alpha.
    return float4(albedoTexture.Sample(albedoSampler, input.uv).rgb * baseColorFactor.rgb, 1.0);
}
