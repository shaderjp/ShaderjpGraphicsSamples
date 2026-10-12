#include "../LightingLab/Scene.hlsli"
struct ParticleInput
{
    VK_LOCATION(0) float4 positionSize : POSITION;
    VK_LOCATION(1) float4 color : COLOR;
};
struct ParticleOutput
{
    float4 position : SV_Position;
    VK_LOCATION(0) float2 uv : TEXCOORD0;
    VK_LOCATION(1) float3 color : COLOR;
};
ParticleOutput VSMain(ParticleInput p, uint id : SV_VertexID)
{
    const float2 corners[6] = {float2(-1, -1), float2(-1, 1), float2(1, 1),
                               float2(-1, -1), float2(1, 1),  float2(1, -1)};
    float3 v = SafeNormalize(cameraPosition.xyz - p.positionSize.xyz),
           right = SafeNormalize(cross(float3(0, 1, 0), v)), up = cross(v, right);
    ParticleOutput o;
    o.uv = corners[id];
    float3 q = p.positionSize.xyz + (right * o.uv.x + up * o.uv.y) * p.positionSize.w;
    o.position = mul(float4(q, 1), viewProjection);
    o.color = p.color.rgb;
    return o;
}
