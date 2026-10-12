#include "Scene.hlsli"
VK_BINDING(1,0) Texture2D<float4> baseTexture:register(t0);
VK_BINDING(2,0) SamplerState materialSampler:register(s0);
VK_BINDING(3,0) Texture2D<float4> normalTexture:register(t1);
VK_BINDING(4,0) Texture2D<float4> ormTexture:register(t2);
VK_BINDING(5,0) Texture2D<float4> emissiveTexture:register(t3);
VK_BINDING(6,0) TextureCube<float4> environmentTexture:register(t4);
VK_BINDING(7,0) TextureCube<float4> irradianceTexture:register(t5);
VK_BINDING(8,0) TextureCube<float4> specularTexture:register(t6);
VK_BINDING(9,0) Texture2D<float2> brdfTexture:register(t7);
VK_BINDING(10,0) Texture2D<float> shadowTexture:register(t8);
VK_BINDING(11,0) SamplerState environmentSampler:register(s1);
struct PointLight {float4 positionRange,colorIntensity;};
VK_BINDING(12,0) StructuredBuffer<PointLight> points:register(t9);
struct Surface {float3 base,normal,emission;float ao,metallic,roughness,alpha;};
Surface ReadMaterial(SurfaceVertex i,bool front)
{
    Surface s;float4 base=baseTexture.Sample(materialSampler,i.uv)*baseColorFactor;
    s.base=base.rgb;s.alpha=base.a;
    if(emissiveFactor.w==1)clip(s.alpha-screen.w);
    float3 orm=ormTexture.Sample(materialSampler,i.uv).rgb;
    s.ao=lerp(1,orm.r,material.w);s.metallic=saturate(orm.b*material.x);s.roughness=max(saturate(orm.g*material.y),.045);
    float3 N=SafeNormalize(i.normal),T=SafeNormalize(i.tangent.xyz-N*dot(N,i.tangent.xyz)),B=i.tangent.w*cross(N,T);
    float3 nt=normalTexture.Sample(materialSampler,i.uv).xyz*2-1;nt.xy*=material.z;
    s.normal=SafeNormalize(T*nt.x+B*nt.y+N*nt.z)*(front?1:-1);
    s.emission=emissiveTexture.Sample(materialSampler,i.uv).rgb*emissiveFactor.rgb;return s;
}
