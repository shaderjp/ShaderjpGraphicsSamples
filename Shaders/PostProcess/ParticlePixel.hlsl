#include "../Common.hlsli"
struct ParticleOutput
{
    float4 position : SV_Position;
    VK_LOCATION(0) float2 uv : TEXCOORD0;
    VK_LOCATION(1) float3 color : COLOR;
};
float4 PSMain(ParticleOutput p) : SV_Target
{
    float a = saturate(1 - dot(p.uv, p.uv));
    return float4(p.color * a * a, 0);
}
