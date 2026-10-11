#include "../Common.hlsli"
VK_BINDING(0, 0) cbuffer Scene : register(b0) { row_major float4x4 mvp; };
ColorOutput VSMain(ColorInput input) {
    ColorOutput output;
    output.position = mul(float4(input.position, 1.0), mvp);
    output.color = input.color;
    return output;
}
