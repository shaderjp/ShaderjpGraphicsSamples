#include "Scene.hlsli"
#include "Dielectric.hlsli"
#include "OpticsTests.hlsli"
struct AreaLight { float4 originArea, edge1Cdf, edge2Probability; uint4 ids; };
VK_BINDING(18,0) StructuredBuffer<AreaLight> areaLights : register(t75);
float3 EmissionAt(uint instanceId, uint triIndex, float2 bary) {
    Primitive p=primitives[instances[instanceId].primitive.x]; Material m=materials[p.offsets.z];
    uint j=p.offsets.y+triIndex*3;
    Vertex a=vertices[p.offsets.x+indices[j]], b=vertices[p.offsets.x+indices[j+1]], c=vertices[p.offsets.x+indices[j+2]];
    float3 w=float3(1-bary.x-bary.y,bary);
    float2 uv=float2(dot(float3(a.positionU.w,b.positionU.w,c.positionU.w),w),dot(float3(a.normalV.w,b.normalV.w,c.normalV.w),w));
    if(m.alpha.y!=0 && textures[NonUniformResourceIndex(m.images.x)].SampleLevel(materialSampler,uv,0).a*m.base.a<m.alpha.x) return 0;
    return textures[NonUniformResourceIndex(m.images.w)].SampleLevel(materialSampler,uv,0).rgb*m.emission.rgb*environment.w;
}
float AreaPdf(AreaLight light, float3 from, float3 to) {
    float3 delta=to-from; float d2=dot(delta,delta);
    float cosine=dot(SafeNormalize(cross(light.edge1Cdf.xyz,light.edge2Probability.xyz)),-SafeNormalize(delta));
    if(light.ids.w!=0)cosine=abs(cosine);
    return cosine>0 && light.originArea.w>0 ? light.edge2Probability.w*d2/(light.originArea.w*cosine) : 0;
}
float VisibilityRay(HitSurface s, float3 direction, float distance) {
    RayDesc ray; ray.Origin=OffsetOrigin(s,direction); ray.Direction=direction; ray.TMin=0;
    ray.TMax=max(distance-screen.w*2,screen.w);
    Payload p=(Payload)0;
    TraceRay(sceneAS,RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|RAY_FLAG_SKIP_CLOSEST_HIT_SHADER|RAY_FLAG_CULL_BACK_FACING_TRIANGLES,255,1,2,1,ray,p);
    return p.distance<0 ? 1 : 0;
}
float3 Direct(HitSurface s, float3 V, uint medium, inout uint rng) {
    float3 result=0; float pdf;
    // Delta emitters cannot be found by continuous BSDF direction sampling.
    if(any(sunColor.rgb*sunColor.w>0))result+=EvaluateSurface(s,V,sunDirection.xyz,pdf)*abs(dot(s.normal,sunDirection.xyz))*sunColor.rgb*sunColor.w*SegmentTransmission(NextMedium(s,sunDirection.xyz,medium),screen.z)*VisibilityRay(s,sunDirection.xyz,screen.z);
    if(controls.x>0) {
        uint id=min(uint(Random(rng)*controls.x),controls.x-1); PointLight light=points[id];
        float3 d=light.positionRange.xyz-s.position; float d2=max(dot(d,d),1e-8), distance=sqrt(d2); float3 L=d/distance;
        float attenuation=light.positionRange.w>0 ? pow(saturate(1-pow(distance/light.positionRange.w,4)),2)/d2 : 1/d2;
        result+=EvaluateSurface(s,V,L,pdf)*abs(dot(s.normal,L))*light.colorIntensity.rgb*light.colorIntensity.w*attenuation*float(controls.x)*SegmentTransmission(NextMedium(s,L,medium),distance)*VisibilityRay(s,L,distance);
    }
    if(lighting.w!=0 && controls.y>0) {
        float choice=Random(rng); uint lo=0,hi=controls.y;
        while(lo<hi){uint mid=lo+(hi-lo)/2;if(choice<areaLights[mid].edge1Cdf.w)hi=mid;else lo=mid+1;}
        AreaLight light=areaLights[min(lo,controls.y-1)]; float2 u=Random2(rng); float root=sqrt(u.x);
        float2 bary=float2(root*(1-u.y),root*u.y);
        float3 position=light.originArea.xyz+light.edge1Cdf.xyz*bary.x+light.edge2Probability.xyz*bary.y;
        float3 d=position-s.position; float distance=length(d); float3 L=SafeNormalize(d);
        float lightPdf=AreaPdf(light,s.position,position);
        if(lightPdf>0) {
            float3 f=EvaluateSurface(s,V,L,pdf);
            result+=f*abs(dot(s.normal,L))*EmissionAt(light.ids.x,light.ids.y,bary)*PowerWeight(lightPdf,pdf)/lightPdf*SegmentTransmission(NextMedium(s,L,medium),distance)*VisibilityRay(s,L,distance);
        }
    }
    return result;
}
[shader("raygeneration")] void RayGeneration() {
    uint2 pixel=DispatchRaysIndex().xy;
    uint rng=Hash(pixel.x+uint(screen.x)*pixel.y)^Hash(controls.z)^Hash(lighting.z);
    if(bsdf.w==1) {OpticsTest(pixel,rng);return;}
    float2 ndc=(float2(pixel)+Random2(rng))/screen.xy*float2(2,-2)+float2(-1,1);
    float4 nearP=mul(float4(ndc,0,1),inverseViewProjection), farP=mul(float4(ndc,1,1),inverseViewProjection);
    nearP/=nearP.w;farP/=farP.w;
    RayDesc ray;ray.Origin=camera.xyz;ray.Direction=SafeNormalize(farP.xyz-camera.xyz);
    ray.TMin=0; // Include medium boundaries before the projection near plane.
    ray.TMax=length(farP.xyz-camera.xyz);
    float3 radiance=0,beta=1,previousPosition=camera.xyz;float previousPdf=0,etaScale=1;
    uint medium=bsdf.x==2?0:bsdf.y;bool previousDelta=true;float opticalDistance=0;uint transmissionCount=0;
    surfaceImage[pixel]=float4(0,0,-1,-1);
    for(uint bounce=0;bounce<=controls.w;++bounce) {
        Payload hit=(Payload)0;hit.distance=-1;
        TraceRay(sceneAS,RAY_FLAG_CULL_BACK_FACING_TRIANGLES,255,0,2,0,ray,hit);
        if(hit.distance<0) {if(medium==0)radiance+=beta*(bsdf.w==2 || bsdf.w==3 ? 1.xxx : environmentTexture.SampleLevel(environmentSampler,EnvDirection(ray.Direction),0).rgb*environment.x);break;}
        float3 position=ray.Origin+hit.distance*ray.Direction;
        if(medium>0)opticalDistance+=length(position-previousPosition);
        beta*=SegmentTransmission(medium,length(position-previousPosition));
        HitSurface s=ReadSurface(hit.instance,hit.triIndex,hit.bary,position,ray.Direction,hit.kind);
        if(bounce==0)surfaceImage[pixel]=float4(s.uv,hit.distance,float(s.material));
        float weight=1;
        uint start=instances[hit.instance].primitive.y;
        if(lighting.w!=0 && lighting.x!=0 && bounce>0 && !previousDelta && start!=0xffffffffu)
            weight=PowerWeight(previousPdf,AreaPdf(areaLights[start+hit.triIndex],previousPosition,position));
        radiance+=beta*s.emission*weight;
        if(bounce==controls.w)break;
        float3 V=-ray.Direction;
        if(lighting.x!=0)radiance+=beta*Direct(s,V,medium,rng);
        ScatterSample scatter=SampleSurface(s,V,rng);
        if(scatter.pdf<=0 || !any(scatter.weight>0))break;
        float3 L=scatter.direction;
        beta*=scatter.weight;
        if(scatter.flags&ScatterTransmission) {++transmissionCount;medium=NextMedium(s,L,medium);etaScale*=scatter.eta*scatter.eta;}
        previousDelta=(scatter.flags&ScatterDelta)!=0;
        if(bounce>=2) {
            float3 rrBeta=beta*etaScale;
            float survival=min(.95,max(rrBeta.x,max(rrBeta.y,rrBeta.z)));
            if(survival<=0 || Random(rng)>=survival)break;
            beta/=survival;
        }
        previousPosition=s.position;previousPdf=scatter.pdf;
        ray.Origin=OffsetOrigin(s,L);ray.Direction=L;ray.TMin=0;ray.TMax=screen.z;
    }
    if(bsdf.w==3)surfaceImage[pixel]=float4(opticalDistance,float(transmissionCount),float(bsdf.y),float(medium));
    reflectionImage[pixel]=float4(radiance,1); // unaccumulated sample for numerical tests
    // No read of undefined contents after reset; this also keeps frame 0 reproducible.
    float3 mean=controls.z==0 ? radiance : outputImage[pixel].rgb+(radiance-outputImage[pixel].rgb)/float(controls.z+1);
    outputImage[pixel]=float4(mean,1);
}
