#include "Scene.hlsli"
VK_BINDING(1, 0) Texture2D<float4> albedoTexture : register(t0);
VK_BINDING(2, 0) SamplerState materialSampler : register(s0);
VK_BINDING(3, 0) Texture2D<float4> normalTexture : register(t1);
VK_BINDING(4, 0) Texture2D<float4> ormTexture : register(t2);
VK_BINDING(5, 0) Texture2D<float4> emissiveTexture : register(t3);

float3 FresnelSchlick(float VoH, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}
float DistributionGGX(float NoH, float alpha)
{
    float a2 = alpha * alpha;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float VisibilitySmithGGX(float NoV, float NoL, float alpha)
{
    float a2 = alpha * alpha;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-6); // G / (4 NoL NoV), height-correlated Smith.
}
float4 PSMain(PbrOutput input) : SV_Target
{
    float3 baseColor = albedoTexture.Sample(materialSampler, input.uv).rgb * baseColorFactor.rgb;
    float3 orm = ormTexture.Sample(materialSampler, input.uv).rgb;
    float metallic = saturate(orm.b * material.x);
    float roughness = saturate(orm.g * material.y);
    float ao = lerp(1.0, orm.r, material.w);
    float3 emissive = emissiveTexture.Sample(materialSampler, input.uv).rgb * emissiveFactor.rgb;
    float3 N = SafeNormalize(input.normal);
    float3 T = SafeNormalize(input.tangent.xyz - N * dot(N, input.tangent.xyz));
    float3 B = input.tangent.w * cross(N, T); // Z reflection in the CPU loader also negates tangent.w.
    float3 normalTS = normalTexture.Sample(materialSampler, input.uv).xyz * 2.0 - 1.0;
    normalTS.xy *= material.z;
    normalTS = SafeNormalize(normalTS);
    if (shading.x != 7.0)
        N = SafeNormalize(T * normalTS.x + B * normalTS.y + N * normalTS.z);
    if (shading.x == 1.0) return float4(baseColor, 1.0);
    if (shading.x == 2.0) return float4(N * 0.5 + 0.5, 1.0);
    if (shading.x == 3.0) return float4(metallic.xxx, 1.0);
    if (shading.x == 4.0) return float4(roughness.xxx, 1.0);
    if (shading.x == 5.0) return float4(ao.xxx, 1.0);
    if (shading.x == 6.0) return float4(emissive, 1.0);
    float3 L = SafeNormalize(lightDirection.xyz);
    float3 V = SafeNormalize(cameraPosition.xyz - input.worldPosition);
    float3 H = SafeNormalize(L + V);
    float NoL = saturate(dot(N, L)), NoV = saturate(dot(N, V));
    float NoH = saturate(dot(N, H)), VoH = saturate(dot(V, H));
    float3 F0 = lerp(0.04.xxx, baseColor, metallic);
    float3 color = 0.0;
    if (NoL > 0.0 && NoV > 0.0)
    {
        float alpha = pow(max(roughness, 0.045), 2.0); // Perceptual roughness -> microfacet alpha.
        float3 F = FresnelSchlick(VoH, F0);
        float3 specular = DistributionGGX(NoH, alpha) * VisibilitySmithGGX(NoV, NoL, alpha) * F;
        float3 diffuse = (1.0 - F) * (1.0 - metallic) * baseColor / PI;
        color = (diffuse + specular) * lightColor.rgb * NoL;
    }
    // Teaching approximation for diffuse ambient. AO affects this term only, never direct light.
    color += ambientExposure.rgb * baseColor * (1.0 - metallic) * ao;
    color += emissive;
    return float4(ToneMap(color), 1.0);
}
