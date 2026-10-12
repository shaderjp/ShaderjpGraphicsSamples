#include "../Common.hlsli"
VK_BINDING(0, 0) cbuffer Post : register(b0)
{
    float4 settings;
    uint4 dimensions;
    float4 simulation;
    float4 emitter;
    uint4 flags;
};
struct Fullscreen
{
    float4 position : SV_Position;
    VK_LOCATION(0) float2 uv : TEXCOORD0;
};
