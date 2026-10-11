#include "../Common.hlsli"
VK_BINDING(0, 0) cbuffer Scene : register(b0)
{
    row_major float4x4 mvp;
    float4 baseColorFactor;
};
