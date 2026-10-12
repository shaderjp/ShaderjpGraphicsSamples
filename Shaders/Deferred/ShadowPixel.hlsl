#include "Resources.hlsli"
void PSMain(float4 position:SV_Position,VK_LOCATION(0) float2 uv:TEXCOORD0){if(emissiveFactor.w==1)clip(baseTexture.Sample(materialSampler,uv).a*baseColorFactor.a-screen.w);}
