#include "DeferredRead.hlsli"
float4 PSMain(Fullscreen i):SV_Target{return float4(ShadePixel(uint2(i.position.xy)),1);}
