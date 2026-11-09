#include "../Deferred/Scene.hlsli"
float4 VSMain(uint vertex : SV_VertexID) : SV_Position
{
    static const uint corners[36] = {0,2,1,1,2,3,4,5,6,5,7,6,0,1,4,1,5,4,2,6,3,3,6,7,0,4,2,2,4,6,1,3,5,3,7,5};
    uint c = corners[vertex];
    float3 p = float3(c&1 ? .5 : -.5, c&2 ? .5 : -.5, c&4 ? .5 : -.5);
    return mul(mul(float4(p,1),world),viewProjection);
}
