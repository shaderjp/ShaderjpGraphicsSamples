#include "Scene.hlsli"
Fullscreen VSMain(uint id:SV_VertexID){Fullscreen o;o.uv=float2((id<<1)&2,id&2);o.position=float4(o.uv*float2(2,-2)+float2(-1,1),1,1);return o;}
