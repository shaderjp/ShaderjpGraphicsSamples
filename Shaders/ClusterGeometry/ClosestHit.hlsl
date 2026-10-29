#include "Scene.hlsli"
float3 Fresnel(float vh, float3 f0)
{
    return f0 + (1 - f0) * pow(1 - vh, 5);
}
float Distribution(float nh, float a)
{
    float a2 = a * a, d = nh * nh * (a2 - 1) + 1;
    return a2 / max(PI * d * d, 1e-12);
}
float Visibility(float nv, float nl, float a)
{
    float a2 = a * a;
    return .5 / max(nl * sqrt(nv * nv * (1 - a2) + a2) + nv * sqrt(nl * nl * (1 - a2) + a2), 1e-6);
}
float3 EvaluateDirect(HitSurface s, float3 V, float3 L, float3 radiance)
{
    float nl = saturate(dot(s.normal, L));
    if (nl <= 0)
        return 0;
    float nv = max(saturate(dot(s.normal, V)), 1e-5);
    float3 H = SafeNormalize(V + L), F = Fresnel(saturate(dot(V, H)), lerp(.04.xxx, s.base, s.metallic));
    return ((1 - F) * (1 - s.metallic) * s.base / PI +
            Distribution(saturate(dot(s.normal, H)), s.roughness * s.roughness) *
                Visibility(nv, nl, s.roughness * s.roughness) * F) *
           radiance * nl;
}
float Shadow(HitSurface s, float3 L, float distance)
{
#if RT_TECHNIQUE >= 2
    if (controls.z == 0)
        return 1;
    RayDesc ray;
    ray.Origin = OffsetOrigin(s, L);
    ray.Direction = L;
    ray.TMin = 0;
    ray.TMax = max(distance - screen.w, screen.w);
    Payload p;
    p.color = 0;
    p.bounce = 0;
    TraceRay(sceneAS,
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER |
                 RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
             255, 1, 0, 1, ray, p);
    return p.color.x;
#else
    return 1;
#endif
}
[shader("closesthit")] void ClosestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attr) {
    HitSurface s = ReadSurface(attr.barycentrics);
    float3 result = s.base, incoming = 0;
    float sunVisibility = 1;
#if RT_TECHNIQUE >= 2
    float3 V = -WorldRayDirection(), direct = 0, diffuse = 0, specular = 0;
    if (lighting.x != 0)
    {
        sunVisibility = Shadow(s, sunDirection.xyz, screen.z);
        direct = EvaluateDirect(s, V, sunDirection.xyz, sunColor.rgb * sunColor.w) * sunVisibility;
        for (uint i = 0; i < controls.x; ++i)
        {
            PointLight l = points[i];
            float3 d = l.positionRange.xyz - s.position;
            float d2 = max(dot(d, d), 1e-4), distance = sqrt(d2), attenuation = 1 / d2;
            if (l.positionRange.w > 0)
                attenuation *= pow(saturate(1 - pow(distance / l.positionRange.w, 4)), 2);
            float3 L = d / distance;
            if (attenuation > 0 && dot(s.normal, L) > 0)
                direct += EvaluateDirect(s, V, L, l.colorIntensity.rgb * l.colorIntensity.w * attenuation) *
                          Shadow(s, L, distance);
        }
    }
    float nv = max(saturate(dot(s.normal, V)), 1e-5);
    float3 f0 = lerp(.04.xxx, s.base, s.metallic);
    if (lighting.y != 0)
    {
        float3 F = Fresnel(nv, f0);
        diffuse = (1 - F) * (1 - s.metallic) * s.base / PI *
                  irradianceTexture.SampleLevel(environmentSampler, EnvDirection(s.normal), 0).rgb * environment.x *
                  s.ao;
        float2 ab = brdfTexture.SampleLevel(environmentSampler, float2(nv, s.roughness), 0);
        specular =
            specularTexture.SampleLevel(environmentSampler, EnvDirection(reflect(-V, s.normal)), s.roughness * 8).rgb *
            (f0 * ab.x + ab.y) * environment.x;
    }
#if RT_TECHNIQUE == 3
    if (payload.bounce == 0 && controls.w != 0)
    {
        float3 R = SafeNormalize(reflect(-V, s.normal));
        RayDesc ray;
        ray.Origin = OffsetOrigin(s, R);
        ray.Direction = R;
        ray.TMin = 0;
        ray.TMax = screen.z;
        Payload reflected;
        reflected.color = 0;
        reflected.bounce = 1;
        TraceRay(sceneAS, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 255, 0, 0, 0, ray, reflected);
        incoming = reflected.color;
        specular = Fresnel(nv, f0) * incoming;
    }
#endif
    result = direct + diffuse + specular + s.emission;
    if (payload.bounce == 0)
    {
        if (controls.y == 1)
            result = s.base;
        if (controls.y == 2)
            result = s.normal * .5 + .5;
        if (controls.y == 4)
            result = direct;
        if (controls.y == 5)
            result = sunVisibility.xxx;
        if (controls.y == 6)
            result = incoming;
    }
#endif
    if (payload.bounce == 0)
    {
        Cluster cluster=clusters[s.cluster];
        float3 weights=float3(1-attr.barycentrics.x-attr.barycentrics.y,attr.barycentrics);
        uint hash=s.cluster*1664525u+1013904223u;
        float3 color=float3(hash&255,(hash>>8)&255,(hash>>16)&255)/255;
        if(lighting.z==1)result=color*.8+.15;
        if(lighting.z==2)result=min(weights.x,min(weights.y,weights.z))<.035?1:color*.18;
        if(lighting.z==3)
        {
            uint t=cluster.extra.y-instances[InstanceID()].primitive.y;
            result=cluster.extra.z==2?(t==32?float3(1,.22,.12):t>=16?float3(.2,1,.3):float3(.15,.35,1)):.25;
            if(cluster.extra.z==2&&min(min(s.uv.x,s.uv.y),min(1-s.uv.x,1-s.uv.y))<.015)result=1;
        }
        if(lighting.z==4)result=frac(float3(.27,.53,.73)*(instances[InstanceID()].primitive.z+1));
        if(lighting.z==5)result=instances[InstanceID()].primitive.w!=0?float3(1,.25,.05):float3(.1,.3,.5);
        surfaceImage[DispatchRaysIndex().xy] = float4(s.uv, RayTCurrent(), float(s.material));
        hitIdsImage[DispatchRaysIndex().xy] = float4(s.cluster,PrimitiveIndex(),InstanceID(),instances[InstanceID()].primitive.z==0xffffffffu?-1.f:float(instances[InstanceID()].primitive.z));
    }
    payload.color = max(result, 0);
}
