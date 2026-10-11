#include "Ui.hlsli"
UiOutput VSMain(UiInput input)
{
    UiOutput output;
    output.position = mul(float4(input.position, 0, 1), ui.projection);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
