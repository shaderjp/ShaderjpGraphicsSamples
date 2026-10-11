#include "Scene.hlsli"
PbrOutput VSMain(PbrInput input)
{
    PbrOutput output;
    float4 position = mul(float4(input.position, 1.0), world);
    output.position = mul(position, viewProjection);
    output.worldPosition = position.xyz;
    output.normal = mul(float4(input.normal, 0.0), normalMatrix).xyz;
    output.tangent = float4(mul(float4(input.tangent.xyz, 0.0), world).xyz, input.tangent.w);
    output.uv = input.uv;
    return output;
}
