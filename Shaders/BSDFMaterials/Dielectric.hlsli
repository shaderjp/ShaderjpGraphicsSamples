#ifndef BSDF_MATERIALS_SCATTERING
#define BSDF_MATERIALS_SCATTERING
// Opaque reference: unchanged Advanced18 Lambert + GGX implementation.
#include "../PathTracing/BSDF.hlsli"
static const uint ScatterReflection=1, ScatterTransmission=2, ScatterDelta=4;
struct ScatterSample { float3 direction, weight; float pdf, eta; uint flags; };
float DielectricFresnel(float cosine,float eta) {
    cosine=clamp(abs(cosine),0,1);
    float sin2T=max(0,1-cosine*cosine)/(eta*eta);
    if(sin2T>=1)return 1;
    float ct=sqrt(max(0,1-sin2T));
    float rp=(eta*cosine-ct)/max(eta*cosine+ct,1e-20);
    float rs=(cosine-eta*ct)/max(cosine+eta*ct,1e-20);
    return .5*(rp*rp+rs*rs);
}
float RelativeEta(HitSurface s,float3 V) { return dot(s.outward,V)>0?s.ior:1/s.ior; }
float G2Dielectric(float nv,float nl,float a) {
    nv=max(abs(nv),1e-10);nl=max(abs(nl),1e-10);
    return 2/(sqrt(1+a*a*(1-nv*nv)/(nv*nv))+sqrt(1+a*a*(1-nl*nl)/(nl*nl)));
}
bool Glass(HitSurface s) { return s.transmission>0 && bsdf.x!=2; }
bool DeltaGlass(HitSurface s,float3 V) { return Glass(s) && (s.roughness==0 || RelativeEta(s,V)==1); }
float3 EvaluateSurface(HitSurface s,float3 V,float3 L,out float pdf) {
    pdf=0;
    if(!Glass(s)) {s.roughness=max(s.roughness,.045);return EvaluateBSDF(s,V,L,pdf);}
    float nv=dot(s.normal,V),nl=dot(s.normal,L);
    if(nv<=0 || nl==0 || DeltaGlass(s,V))return 0;
    bool reflected=nl>0;
    if((dot(s.geometric,L)>0)!=reflected || (!reflected && bsdf.x==1))return 0;
    float eta=RelativeEta(s,V);
    float3 h=V+(reflected?1:eta)*L;
    if(dot(h,h)<1e-18)return 0;
    h=normalize(h);if(dot(h,s.normal)<0)h=-h;
    float vh=dot(V,h),lh=dot(L,h);
    if(vh<=0 || lh*nl<=0)return 0;
    float a=max(s.roughness*s.roughness,1e-4),D=Dggx(saturate(dot(s.normal,h)),a);
    float F=DielectricFresnel(vh,eta),G=G2Dielectric(nv,nl,a);
    float visiblePdf=D*G1(nv,a)*abs(vh)/nv;
    if(reflected) {
        float probability=bsdf.x==1?(F>0?1:0):F;
        pdf=probability*visiblePdf/(4*abs(vh));
        return (F*D*G/(4*abs(nv*nl))).xxx;
    }
    float denom=lh+vh/eta;
    if(abs(denom)<1e-12)return 0;
    float jacobian=abs(lh)/(denom*denom);
    pdf=(1-F)*visiblePdf*jacobian;
    // Radiance transport correction, separate from the solid-angle Jacobian.
    return ((1-F)*D*G*abs(lh*vh/(nv*nl*denom*denom))/(eta*eta)).xxx;
}
ScatterSample SampleSurface(HitSurface s,float3 V,inout uint rng) {
    ScatterSample r=(ScatterSample)0;r.eta=1;
    if(!Glass(s)) {
        s.roughness=max(s.roughness,.045);r.direction=SampleBSDF(s,V,rng);
        float3 f=EvaluateBSDF(s,V,r.direction,r.pdf);
        if(r.pdf>0)r.weight=f*max(0,dot(s.normal,r.direction))/r.pdf;
        r.flags=ScatterReflection;return r;
    }
    float eta=RelativeEta(s,V);
    float3 T,B;Basis(s.normal,T,B);float3 v=float3(dot(V,T),dot(V,B),dot(V,s.normal));
    if(v.z<=0)return r;
    bool delta=DeltaGlass(s,V);
    float3 h=delta?float3(0,0,1):SampleGGX(v,max(s.roughness*s.roughness,1e-4),Random2(rng));
    float F=DielectricFresnel(dot(v,h),eta);
    float probability=bsdf.x==1?(F>0?1:0):F;
    if(bsdf.x==1 && F==0)return r;
    bool reflected=Random(rng)<probability;
    float3 l=reflected?reflect(-v,h):refract(-v,h,1/eta);
    if(dot(l,l)<1e-12)return r;
    r.direction=SafeNormalize(T*l.x+B*l.y+s.normal*l.z);
    if((dot(s.geometric,r.direction)>0)!=reflected)return (ScatterSample)0;
    r.flags=reflected?ScatterReflection:ScatterTransmission;r.eta=reflected?1:eta;
    if(delta) {
        r.pdf=reflected?probability:1-probability;
        r.weight=((reflected?F:(1-F)/(eta*eta))/max(r.pdf,1e-20)).xxx;
        r.flags|=ScatterDelta;
    } else {
        float3 f=EvaluateSurface(s,V,r.direction,r.pdf);
        if(r.pdf>0)r.weight=f*abs(dot(s.normal,r.direction))/r.pdf;
    }
    return r;
}
uint NextMedium(HitSurface s,float3 L,uint medium) {
    if(Glass(s) && dot(s.geometric,L)<0)return dot(s.outward,L)<0?s.material+1:0;
    return medium;
}
float3 SegmentTransmission(uint medium,float distance) {
    return medium>0?exp(-materials[medium-1].absorption.rgb*max(0,distance)):1.xxx;
}
#endif
