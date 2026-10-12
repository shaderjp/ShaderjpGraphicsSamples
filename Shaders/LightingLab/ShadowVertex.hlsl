#include "Scene.hlsli"
float4 VSMain(Input i):SV_Position{return mul(mul(float4(i.position,1),world),lightViewProjection);}
