#ifndef PT_BSDF
#define PT_BSDF
uint Hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16); }
float Random(inout uint state) { state = state * 747796405u + 2891336453u; uint w = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u; return float((w ^ (w >> 22u)) >> 8) * (1.0 / 16777216.0); }
float2 Random2(inout uint state) { float a = Random(state); return float2(a, Random(state)); }
void Basis(float3 n, out float3 t, out float3 b) { t = SafeNormalize(cross(abs(n.z) < .999 ? float3(0,0,1) : float3(1,0,0), n)); b = cross(n,t); }
float3 Fresnel(float vh, float3 f0) { return f0 + (1-f0) * pow(1-saturate(vh),5); }
float Dggx(float nh, float alpha) { float a2=alpha*alpha, d=nh*nh*(a2-1)+1; return a2 / max(PI*d*d, 1e-18); }
float G1(float nv, float a) { return 2*nv / max(nv + sqrt(a*a+(1-a*a)*nv*nv), 1e-12); }
float SpecProbability(HitSurface s) {
    if (s.metallic >= 1) return 1;
    float3 f0=lerp(.04.xxx,s.base,s.metallic);
    float spec=dot(f0,float3(.2126,.7152,.0722));
    float diff=(1-s.metallic)*dot(s.base,float3(.2126,.7152,.0722));
    return clamp(spec/max(spec+diff,1e-8),.1,.9);
}
float3 EvaluateBSDF(HitSurface s, float3 V, float3 L, out float pdf) {
    pdf=0;
    float nv=dot(s.normal,V), nl=dot(s.normal,L);
    if(nv<=0 || nl<=0 || dot(s.geometric,L)<=0) return 0;
    float3 H=SafeNormalize(V+L); float nh=saturate(dot(s.normal,H)), vh=saturate(dot(V,H));
    float a=s.roughness*s.roughness, a2=a*a, d=Dggx(nh,a);
    float vis=.5/max(nl*sqrt(nv*nv*(1-a2)+a2)+nv*sqrt(nl*nl*(1-a2)+a2),1e-12);
    float3 F=Fresnel(vh,lerp(.04.xxx,s.base,s.metallic));
    float p=SpecProbability(s);
    pdf=(1-p)*nl/PI+p*d*G1(nv,a)/max(4*nv,1e-12);
    return (1-F)*(1-s.metallic)*s.base/PI+d*vis*F;
}
// Isotropic Trowbridge-Reitz visible-normal sampling (Heitz / pbrt).
float3 SampleGGX(float3 v, float alpha, float2 u) {
    float3 vh=SafeNormalize(float3(alpha*v.xy,v.z));
    float lensq=dot(vh.xy,vh.xy);
    float3 t1=lensq>0 ? float3(-vh.y,vh.x,0)*rsqrt(lensq) : float3(1,0,0), t2=cross(vh,t1);
    float r=sqrt(u.x), phi=2*PI*u.y, p1=r*cos(phi), p2=r*sin(phi), h=.5*(1+vh.z);
    p2=(1-h)*sqrt(max(0,1-p1*p1))+h*p2;
    float3 nh=p1*t1+p2*t2+sqrt(max(0,1-p1*p1-p2*p2))*vh;
    return SafeNormalize(float3(alpha*nh.xy,max(0,nh.z)));
}
float3 SampleBSDF(HitSurface s, float3 V, inout uint rng) {
    float3 T,B; Basis(s.normal,T,B); float3 l;
    float choose=Random(rng); float2 u=Random2(rng);
    if(choose<SpecProbability(s)) {
        float3 v=float3(dot(V,T),dot(V,B),dot(V,s.normal));
        l=reflect(-v,SampleGGX(v,s.roughness*s.roughness,u));
    } else {
        float r=sqrt(u.x), phi=2*PI*u.y; l=float3(r*cos(phi),r*sin(phi),sqrt(max(0,1-u.x)));
    }
    return SafeNormalize(T*l.x+B*l.y+s.normal*l.z);
}
float PowerWeight(float a, float b) { float scale=max(a,b); if(scale<=0)return 0; a/=scale;b/=scale;return a*a/(a*a+b*b); }
#endif
