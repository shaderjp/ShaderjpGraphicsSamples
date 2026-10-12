#include "Scene.hlsli"
struct ShadowVertex {float4 position:SV_Position;VK_LOCATION(0) float2 uv:TEXCOORD0;};
ShadowVertex VSMain(Input i){ShadowVertex o;o.position=mul(mul(float4(i.position,1),world),lightViewProjection);o.uv=i.uv;return o;}
