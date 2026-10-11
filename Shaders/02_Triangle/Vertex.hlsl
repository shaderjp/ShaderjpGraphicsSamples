#include "../Common.hlsli"
ColorOutput VSMain(ColorInput input) {
    ColorOutput output;
    output.position = float4(input.position, 1.0);
    output.color = input.color;
    return output;
}
