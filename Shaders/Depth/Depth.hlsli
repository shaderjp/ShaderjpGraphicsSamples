#include "../Common.hlsli"
struct DepthConstants
{
    row_major float4x4 view, projection, lightView, lightProjection;
    float4 range, mode, display, extent;
};
VK_BINDING(0,0) ConstantBuffer<DepthConstants> c : register(b0);
VK_BINDING(1,0) Texture2D<float> depth0 : register(t0);
VK_BINDING(3,0) Texture2D<float4> data0 : register(t1);
VK_BINDING(4,0) Texture2D<float4> color0 : register(t2);
VK_BINDING(5,0) Texture2D<float> depth1 : register(t3);
VK_BINDING(6,0) Texture2D<float4> data1 : register(t4);
VK_BINDING(7,0) Texture2D<float4> color1 : register(t5);
VK_BINDING(8,0) Texture2D<float> shadowDepth : register(t6);
VK_BINDING(9,0) Texture2D<float> shadowLinear : register(t7);
struct Input
{
    VK_LOCATION(0) float3 position : POSITION;
    VK_LOCATION(1) float3 color : COLOR;
    VK_LOCATION(2) float id : TEXCOORD0;
};
struct Output
{
    float4 position : SV_Position;
    VK_LOCATION(0) float3 color : COLOR;
    VK_LOCATION(1) float3 world : TEXCOORD0;
    VK_LOCATION(2) float viewZ : TEXCOORD1;
    VK_LOCATION(3) nointerpolation float id : TEXCOORD2;
};
struct FullOutput {float4 position:SV_Position; VK_LOCATION(0) float2 uv:TEXCOORD0;};
float Decode(float d,bool reverse,bool ortho,bool infinite,float n,float f)
{
    if(ortho)return reverse?f-d*(f-n):n+d*(f-n);
    if(infinite)return n/max(reverse?d:1-d,1e-30);
    return n*f/(reverse?n+d*(f-n):f-d*(f-n));
}
