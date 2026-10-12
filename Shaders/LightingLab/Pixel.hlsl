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
float3 Fresnel(float vh,float3 f0){return f0+(1-f0)*pow(1-vh,5);}
float D(float nh,float a){float a2=a*a,d=nh*nh*(a2-1)+1;return a2/(PI*d*d);}
float Visibility(float nv,float nl,float a){float a2=a*a;return .5/max(nl*sqrt(nv*nv*(1-a2)+a2)+nv*sqrt(nl*nl*(1-a2)+a2),1e-6);}
float ShadowVisibility(float3 p)
{
    if(shadow.x==0||environment.w==6)return 1;
    float4 q=mul(float4(p,1),lightViewProjection);q.xyz/=q.w;
    float2 uv=q.xy*float2(.5,-.5)+.5;
    if(any(uv<0)||any(uv>1)||q.z<=0||q.z>=1)return 1;
    int radius=(shadow.y!=0&&environment.w!=7)||environment.w==8?1:0;
    float visible=0;
    for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x)
    {int2 pixel=clamp(int2(uv*shadow.w)+int2(x,y),0,int(shadow.w)-1);
        visible+=q.z-shadow.z<=shadowTexture.Load(int3(pixel,0));}
    return visible/float((radius*2+1)*(radius*2+1));
}
float4 PSMain(Output i):SV_Target
{
    float3 base=baseTexture.Sample(materialSampler,i.uv).rgb*baseColorFactor.rgb;
    float3 orm=ormTexture.Sample(materialSampler,i.uv).rgb;
    float m=saturate(orm.b*material.x),r=max(saturate(orm.g*material.y),.045);
    float3 N=SafeNormalize(i.normal),T=SafeNormalize(i.tangent.xyz-N*dot(N,i.tangent.xyz)),B=i.tangent.w*cross(N,T);
    float3 nt=normalTexture.Sample(materialSampler,i.uv).xyz*2-1;nt.xy*=material.z;nt=SafeNormalize(nt);
    N=SafeNormalize(T*nt.x+B*nt.y+N*nt.z);
    float3 V=SafeNormalize(cameraPosition.xyz-i.worldPosition),L=SafeNormalize(lightDirection.xyz),H=SafeNormalize(V+L);
    float nl=saturate(dot(N,L)),nv=max(saturate(dot(N,V)),1e-5),nh=saturate(dot(N,H)),vh=saturate(dot(V,H));
    float3 f0=lerp(.04.xxx,base,m),F=Fresnel(vh,f0);
    float3 direct=0;
    if(nl>0)direct=((1-F)*(1-m)*base/PI+D(nh,r*r)*Visibility(nv,nl,r*r)*F)*lightColor.rgb*nl;
#if LAB_TECHNIQUE >= 13
    direct*=ShadowVisibility(i.worldPosition);
    if(environment.w==5){float2 uv=(i.position.xy-float2(shading.z,0))/shading.xy;float z=shadowTexture.Load(int3(clamp(int2(uv*shadow.w),0,int(shadow.w)-1),0));return float4(z.xxx,1);}
#endif
    float3 R=EnvironmentDirection(reflect(-V,N));
    if(environment.w==1)return float4(ToneMap(environmentTexture.SampleLevel(environmentSampler,R,0).rgb*environment.x),1);
    float3 diffuse=0,specular=0;
#if LAB_TECHNIQUE >= 11
    float3 envF=Fresnel(nv,f0);
    float ao=lerp(1,orm.r,material.w);
    diffuse=(1-envF)*(1-m)*base/PI*irradianceTexture.SampleLevel(environmentSampler,EnvironmentDirection(N),0).rgb*environment.x*ao;
#endif
#if LAB_TECHNIQUE >= 12
    float2 ab=brdfTexture.SampleLevel(environmentSampler,float2(nv,r),0);
    specular=specularTexture.SampleLevel(environmentSampler,R,r*8).rgb*(f0*ab.x+ab.y)*environment.x;
#endif
    float3 emissive=emissiveTexture.Sample(materialSampler,i.uv).rgb*emissiveFactor.rgb;
    float3 color=environment.w==2?direct:environment.w==3?diffuse:environment.w==4?specular:direct+diffuse+specular+emissive;
    return float4(ToneMap(color),1);
}
