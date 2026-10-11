#include "Common.hlsli"
VK_BINDING(0, 0) cbuffer Scene : register(b0)
{
    row_major float4x4 world;
    row_major float4x4 viewProjection;
    row_major float4x4 normalMatrix;
    float4 baseColorFactor;
    float4 cameraPosition;
    float4 lightDirection;
    float4 lightColor;
    float4 ambientExposure;
    float4 material;
    float4 emissiveFactor;
    float4 shading;
};
static const float PI = 3.14159265359;
struct LitInput
{
    VK_LOCATION(0) float3 position : POSITION;
    VK_LOCATION(1) float2 uv : TEXCOORD0;
    VK_LOCATION(2) float3 normal : NORMAL;
};
struct LitOutput
{
    float4 position : SV_Position;
    VK_LOCATION(0) float2 uv : TEXCOORD0;
    VK_LOCATION(1) float3 worldPosition : TEXCOORD1;
    VK_LOCATION(2) float3 normal : TEXCOORD2;
};
float3 SafeNormalize(float3 v)
{
    return v * rsqrt(max(dot(v, v), 1e-12));
}
float3 ToneMap(float3 linearColor)
{
    float3 exposed = max(linearColor * ambientExposure.w, 0.0);
    return exposed / (1.0 + exposed); // Reinhard, followed by the sRGB output view's encoding.
}
