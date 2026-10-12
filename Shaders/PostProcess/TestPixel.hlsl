#include "Post.hlsli"
float4 PSMain(Fullscreen i) : SV_Target
{
    float3 c = flags.z == 2                                             ? float3(4, 2, .5)
               : flags.z == 3 && all(int2(i.position.xy) == int2(0, 0)) ? 16.xxx
                                                                        : 0.xxx;
    return float4(c, 1);
}
