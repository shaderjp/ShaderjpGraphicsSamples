#include "Scene.hlsli"
SurfaceVertex VSMain(Input i){SurfaceVertex o;float4 p=mul(float4(i.position,1),world);o.position=mul(p,viewProjection);o.worldPosition=p.xyz;o.uv=i.uv;o.normal=mul(float4(i.normal,0),normalMatrix).xyz;o.tangent=float4(mul(float4(i.tangent.xyz,0),world).xyz,i.tangent.w);return o;}
