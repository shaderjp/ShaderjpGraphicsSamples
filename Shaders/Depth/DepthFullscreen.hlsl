#include "Depth.hlsli"
FullOutput VSMain(uint id:SV_VertexID)
{
    FullOutput o;o.uv=float2((id<<1)&2,id&2);
    o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;
}
