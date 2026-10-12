#include "Scene.hlsli"
VK_BINDING(6,0) TextureCube<float4> environmentTexture:register(t4);
VK_BINDING(11,0) SamplerState environmentSampler:register(s1);
VK_BINDING(10,0) Texture2D<float> shadowTexture:register(t8);
float4 PSMain(float4 position:SV_Position,VK_LOCATION(0) float3 direction:TEXCOORD0):SV_Target
{
#if LAB_TECHNIQUE >= 13
    if(environment.w==5){float2 uv=(position.xy-float2(shading.z,0))/shading.xy;
        float z=shadowTexture.Load(int3(clamp(int2(uv*shadow.w),0,int(shadow.w)-1),0));return float4(z.xxx,1);}
#endif
    return float4(ToneMap(environmentTexture.SampleLevel(environmentSampler,EnvironmentDirection(SafeNormalize(direction)),0).rgb*environment.x),1);
}
