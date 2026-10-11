#include "../Common.hlsli"
TextureOutput VSMain(TextureInput input) {
    TextureOutput output;
    output.position = float4(input.position, 1.0);
    output.uv = input.uv;
    return output;
}
