#include "Scene.hlsli"
struct SkyOutput{float4 position:SV_Position;VK_LOCATION(0) float3 direction:TEXCOORD0;};
SkyOutput VSMain(uint id:SV_VertexID){SkyOutput o;float2 p=float2((id<<1)&2,id&2)*float2(2,-2)+float2(-1,1);
    o.position=float4(p,1,1);float4 q=mul(float4(p,1,1),inverseViewProjection);o.direction=q.xyz/q.w-cameraPosition.xyz;return o;}
