#include "../Common.hlsli"
struct UiInput
{
    VK_LOCATION(0) float2 position : POSITION;
    VK_LOCATION(1) float2 uv : TEXCOORD;
    VK_LOCATION(2) float4 color : COLOR;
};
struct UiOutput
{
    float4 position : SV_Position;
    VK_LOCATION(0) float2 uv : TEXCOORD;
    VK_LOCATION(1) float4 color : COLOR;
};
struct UiConstants
{
    row_major float4x4 projection;
};
VK_BINDING(0, 0) ConstantBuffer<UiConstants> ui : register(b0);
