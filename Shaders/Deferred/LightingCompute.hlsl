#include "DeferredRead.hlsli"
#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
VK_BINDING(18,0) RWTexture2D<float4> hdrOutput:register(u0);
[numthreads(8,8,1)]void CSMain(uint3 id:SV_DispatchThreadID){if(any(id.xy>=uint2(screen.xy)))return;hdrOutput[id.xy]=float4(ShadePixel(id.xy),1);}
