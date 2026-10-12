#include "Post.hlsli"
VK_BINDING(1,0) Texture2D<float4> sceneTexture:register(t0);
VK_BINDING(2,0) SamplerState linearSampler:register(s0);
VK_BINDING(3,0) Texture2D<float4> bloomTexture:register(t1);
float4 PSMain(Fullscreen i):SV_Target
{
    float3 c=sceneTexture.SampleLevel(linearSampler,i.uv,0).rgb;
    if(flags.x!=0)c+=bloomTexture.SampleLevel(linearSampler,i.uv,0).rgb*settings.z;
    if(flags.w!=0)return float4(saturate(c),1);
    c=max(c*settings.x,0);return float4(c/(1+c),1);
}
