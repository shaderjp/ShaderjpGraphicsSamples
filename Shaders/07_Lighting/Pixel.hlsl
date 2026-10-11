#include "../Lighting.hlsli"
VK_BINDING(1, 0) Texture2D<float4> albedoTexture : register(t0);
VK_BINDING(2, 0) SamplerState materialSampler : register(s0);
float4 PSMain(LitOutput input) : SV_Target
{
    float3 albedo = albedoTexture.Sample(materialSampler, input.uv).rgb * baseColorFactor.rgb;
    if (shading.x == 2.0)
        return float4(albedo, 1.0); // Same unlit display as Sample 06; no tone mapping.
    float3 N = SafeNormalize(input.normal); // Renormalize after interpolation.
    float3 L = SafeNormalize(lightDirection.xyz);
    float3 V = SafeNormalize(cameraPosition.xyz - input.worldPosition);
    float NoL = saturate(dot(N, L));
    float3 diffuse = albedo / PI * NoL;
    float3 specular = 0.0;
    if (shading.x == 1.0 && NoL > 0.0 && dot(N, V) > 0.0)
    {
        float3 H = SafeNormalize(L + V);
        specular = shading.z * pow(saturate(dot(N, H)), shading.y);
    }
    // The Blinn-Phong lobe is deliberately an artistic, non-energy-conserving model.
    float3 color = ambientExposure.rgb * albedo + lightColor.rgb * (diffuse + specular);
    return float4(ToneMap(color), 1.0);
}
