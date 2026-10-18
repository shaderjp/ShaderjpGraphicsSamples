#include "Scene.hlsli"
struct Fullscreen
{
    float4 position : SV_Position;
    VK_LOCATION(0) float2 uv : TEXCOORD0;
};
Fullscreen VSMain(uint id : SV_VertexID)
{
    Fullscreen o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 PSMain(Fullscreen i) : SV_Target0
{
    float3 c = gbuffer[0].Load(int3(uint2(i.position.xy), 0)).rgb;
    return float4(c / (1 + c), 1);
}
