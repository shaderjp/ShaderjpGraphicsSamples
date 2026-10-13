#include "../Common.hlsli"
VK_BINDING(0, 0) cbuffer Scene : register(b0)
{
    row_major float4x4 mvp;
    float4 baseColor;
    uint4 counts;
};
struct Vertex
{
    float4 positionU, normalV;
};
VK_BINDING(1, 0) Texture2D<float4> albedo : register(t0);
VK_BINDING(2, 0) SamplerState linearSampler : register(s0);
VK_BINDING(3, 0) StructuredBuffer<Vertex> vertices : register(t1);
VK_BINDING(4, 0) StructuredBuffer<uint> indices : register(t2);
struct Output
{
    float4 position : SV_Position;
    VK_LOCATION(0) float2 uv : TEXCOORD0;
};
Output Transform(uint index)
{
    Vertex v = vertices[index];
    Output o;
    o.position = mul(float4(v.positionU.xyz, 1), mvp);
    o.uv = float2(v.positionU.w, v.normalV.w);
    return o;
}
