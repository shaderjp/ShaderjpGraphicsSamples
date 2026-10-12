#include "Lighting.hlsli"
VK_BINDING(13,0) Texture2D<float4> gBaseAo:register(t10);
VK_BINDING(14,0) Texture2D<float2> gNormal:register(t11);
VK_BINDING(15,0) Texture2D<float2> gMetalRough:register(t12);
VK_BINDING(16,0) Texture2D<float3> gEmission:register(t13);
VK_BINDING(17,0) Texture2D<float> gDepth:register(t14);
float3 ShadePixel(uint2 pixel)
{
    if(controls.y==9){uint2 xy=min(uint2((float2(pixel)+.5)/screen.xy*shadow.w),uint2(shadow.w-1,shadow.w-1));return shadowTexture.Load(int3(xy,0)).xxx;}
    float depth=gDepth.Load(int3(pixel,0));if(depth==1)return 0;
    float4 base=gBaseAo.Load(int3(pixel,0));float2 normal=gNormal.Load(int3(pixel,0)),mr=gMetalRough.Load(int3(pixel,0));
    Surface s;s.base=base.rgb;s.ao=base.a;s.normal=DecodeNormal(normal);s.metallic=mr.r;s.roughness=max(mr.g,.045);s.emission=gEmission.Load(int3(pixel,0));s.alpha=1;
    if(controls.y==1)return s.base;if(controls.y==2)return s.ao.xxx;if(controls.y==3)return float3(normal*.5+.5,0);if(controls.y==4)return s.normal*.5+.5;
    if(controls.y==5)return mr.r.xxx;if(controls.y==6)return mr.g.xxx;if(controls.y==7)return s.emission;if(controls.y==8)return depth.xxx;
    return Shade(s,ReconstructPosition(pixel,depth));
}
