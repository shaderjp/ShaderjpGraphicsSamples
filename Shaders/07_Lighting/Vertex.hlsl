#include "../Lighting.hlsli"
LitOutput VSMain(LitInput input)
{
    LitOutput output;
    float4 position = mul(float4(input.position, 1.0), world);
    output.position = mul(position, viewProjection);
    output.worldPosition = position.xyz;
    output.normal = mul(float4(input.normal, 0.0), normalMatrix).xyz;
    output.uv = input.uv;
    return output;
}
