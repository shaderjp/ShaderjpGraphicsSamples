#include "../Lighting.hlsli"
struct PbrInput
{
    VK_LOCATION(0) float3 position : POSITION;
    VK_LOCATION(1) float2 uv : TEXCOORD0;
    VK_LOCATION(2) float3 normal : NORMAL;
    VK_LOCATION(3) float4 tangent : TANGENT;
};
struct PbrOutput
{
    float4 position : SV_Position;
    VK_LOCATION(0) float2 uv : TEXCOORD0;
    VK_LOCATION(1) float3 worldPosition : TEXCOORD1;
    VK_LOCATION(2) float3 normal : TEXCOORD2;
    VK_LOCATION(3) float4 tangent : TEXCOORD3;
};
