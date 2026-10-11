#include "../Common.hlsli"
VK_BINDING(0, 0) cbuffer Scene : register(b0) { row_major float4x4 mvp; };
TextureOutput VSMain(TextureInput input) {
    TextureOutput output;
    output.position = mul(float4(input.position, 1.0), mvp);
    output.uv = input.uv;
    return output;
}
