#include "Scene.hlsli"
float4 PSMain(Output i) : SV_Target
{
    return float4(albedo.Sample(linearSampler, i.uv).rgb * baseColor.rgb, 1);
}
