#include "Resources.hlsli"
#ifdef INLINE_RT
#include "../InlineRaytracing/Query.hlsli"
#endif
#ifdef LIGHT_CULLING
#include "../LightCulling/Grid.hlsli"
#endif
float3 Fresnel(float vh,float3 f0){return f0+(1-f0)*pow(1-vh,5);}
float Distribution(float nh,float a){float a2=a*a,d=nh*nh*(a2-1)+1;return a2/max(PI*d*d,1e-12);}
float Visibility(float nv,float nl,float a){float a2=a*a;return .5/max(nl*sqrt(nv*nv*(1-a2)+a2)+nv*sqrt(nl*nl*(1-a2)+a2),1e-6);}
float ShadowVisibility(float3 p)
{
    if(shadow.x==0)return 1;float4 q=mul(float4(p,1),lightViewProjection);q.xyz/=q.w;
    float2 uv=q.xy*float2(.5,-.5)+.5;if(any(uv<0)||any(uv>1)||q.z<=0||q.z>=1)return 1;
    int radius=shadow.y!=0?1:0;float visible=0;
    for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x){int2 pixel=clamp(int2(uv*shadow.w)+int2(x,y),0,int(shadow.w)-1);visible+=q.z-shadow.z<=shadowTexture.Load(int3(pixel,0));}
    return visible/float((radius*2+1)*(radius*2+1));
}
float3 EvaluateDirect(Surface s,float3 V,float3 L,float3 radiance)
{
    float nl=saturate(dot(s.normal,L));if(nl<=0)return 0;
    float nv=max(saturate(dot(s.normal,V)),1e-5);float3 H=SafeNormalize(V+L),F=Fresnel(saturate(dot(V,H)),lerp(.04.xxx,s.base,s.metallic));
    return ((1-F)*(1-s.metallic)*s.base/PI+Distribution(saturate(dot(s.normal,H)),s.roughness*s.roughness)*Visibility(nv,nl,s.roughness*s.roughness)*F)*radiance*nl;
}
float3 ShadeSurface(Surface s,float3 p,uint cell)
{
    float3 V=SafeNormalize(cameraPosition.xyz-p),direct=0,diffuse=0,specular=0;
#ifdef INLINE_RT
    float visibility=inlineControls.x!=0 ? InlineVisibility(p,s.normal,sunDirection.xyz,inlineRay.y) : ShadowVisibility(p);
#else
    float visibility=ShadowVisibility(p);
#endif
    if(controls.z!=0){direct=EvaluateDirect(s,V,sunDirection.xyz,sunColor.rgb*sunColor.w)*visibility;
        uint count=controls.x;
#ifdef LIGHT_CULLING
        uint2 header=uint2(controls.x,1);
        if(cell!=0xffffffff && gridControls.x!=0)header=lightHeaders[cell];
        // A full list is the correctness fallback, including unbounded lights and overflow.
        count=header.y!=0 ? controls.x : header.x;
#endif
        for(uint item=0;item<count;++item){uint light=item;
#ifdef LIGHT_CULLING
            if(header.y==0)light=lightIndices[cell*gridSize.w+item];
#endif
            PointLight l=points[light];float3 d=l.positionRange.xyz-p;float d2=max(dot(d,d),1e-4);float attenuation=1/d2;
            if(l.positionRange.w>0)attenuation*=pow(saturate(1-pow(sqrt(d2)/l.positionRange.w,4)),2);
            float pointVisibility=1;
#ifdef INLINE_RT
            if(attenuation>0 && dot(s.normal,d)>0) pointVisibility=InlinePointVisibility(p,s.normal,SafeNormalize(d),sqrt(d2),light);
#endif
            direct+=EvaluateDirect(s,V,SafeNormalize(d),l.colorIntensity.rgb*l.colorIntensity.w*attenuation)*pointVisibility;}}
    if(controls.w!=0){float nv=max(saturate(dot(s.normal,V)),1e-5);float3 f0=lerp(.04.xxx,s.base,s.metallic),envF=Fresnel(nv,f0);
        diffuse=(1-envF)*(1-s.metallic)*s.base/PI*irradianceTexture.SampleLevel(environmentSampler,EnvironmentDirection(s.normal),0).rgb*environment.x*s.ao;
        float2 ab=brdfTexture.SampleLevel(environmentSampler,float2(nv,s.roughness),0);
        specular=specularTexture.SampleLevel(environmentSampler,EnvironmentDirection(reflect(-V,s.normal)),s.roughness*8).rgb*(f0*ab.x+ab.y)*environment.x;}
#if defined(INLINE_RT) && INLINE_SAMPLE == 27
    if(inlineControls.z!=0 || inlineControls.w==1){float3 incoming=InlineReflection(s,p,V);if(inlineControls.w==1)return incoming;specular=Fresnel(max(saturate(dot(s.normal,V)),1e-5),lerp(.04.xxx,s.base,s.metallic))*incoming;}
#endif
    if(controls.y==10)return direct;if(controls.y==11)return diffuse;if(controls.y==12)return specular;if(controls.y==13)return visibility.xxx;
    return max(direct+diffuse+specular+s.emission,0);
}
float3 Shade(Surface s,float3 p){return ShadeSurface(s,p,0xffffffff);}
