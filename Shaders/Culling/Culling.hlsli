#include "../Common.hlsli"
VK_BINDING(0,0) cbuffer Culling : register(b0)
{
    row_major float4x4 cullViewProjection;
    uint4 dimensions; // viewport and padded dimensions
    uint4 level; // source offset, destination offset, source dimensions
    uint4 info; // count, operation, selected mip, debug source
};
struct Bounds {float4 minimum,maximum;};
VK_BINDING(1,0) Texture2D<float> sourceDepth : register(t0);
VK_BINDING(3,0) StructuredBuffer<Bounds> bounds : register(t1);
VK_BINDING(4,0) StructuredBuffer<float> debugDepth : register(t2);
VK_BINDING(5,0) RWStructuredBuffer<float> pyramid : register(u0);
VK_BINDING(6,0) RWByteAddressBuffer predicates : register(u1);
