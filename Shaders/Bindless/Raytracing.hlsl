#include "Scene.hlsli"
struct Payload
{
    float3 color;
    uint depth;
    float distance;
    uint instance;
    float2 uv;
};
Surface HitSurface(float2 bary)
{
    Instance inst = GetInstance(InstanceID());
    Geometry g = GetGeometry(inst.ids.x);
    uint base = PrimitiveIndex() * 3;
    uint3 ix = uint3(GetIndex(g, base), GetIndex(g, base + 1), GetIndex(g, base + 2));
    return ReadSurface(InstanceID(), ix, float3(1 - bary.x - bary.y, bary), HitKind() != HIT_KIND_TRIANGLE_BACK_FACE, 0,
                       0, true);
}
#if !defined(__spirv__) || defined(GALLERY_ENTRY_Alpha)
[shader("anyhit")] void Alpha(inout Payload payload, in BuiltInTriangleIntersectionAttributes attr) {
    Instance inst = GetInstance(InstanceID());
    Material m = GetMaterial(inst.ids.y);
    if (m.alpha.y == 0)
        return;
    Geometry g = GetGeometry(inst.ids.x);
    uint base = PrimitiveIndex() * 3;
    Vertex a = GetVertex(g, GetIndex(g, base)), b = GetVertex(g, GetIndex(g, base + 1)),
           c = GetVertex(g, GetIndex(g, base + 2));
    float3 weights = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics);
    float2 uv = float2(dot(float3(a.positionU.w, b.positionU.w, c.positionU.w), weights),
                       dot(float3(a.normalV.w, b.normalV.w, c.normalV.w), weights));
    if (SampleImage(m, 0, uv, 0, 0, true).a * m.base.a < m.alpha.x)
        IgnoreHit();
}
#endif
float Visibility(Surface s)
{
    if (!controls.z)
        return 1;
    Payload p;
    p.color = 0;
    p.depth = 2;
    RayDesc ray;
    ray.Origin = s.position + s.normal * .002;
    ray.Direction = Normalize(-sunDirection.xyz);
    ray.TMin = .001;
    ray.TMax = 250;
    TraceRay(sceneAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 255, 0, 1, 0, ray,
             p);
    return p.color.x;
}
float3 Reflection(Surface s)
{
    Payload p;
    p.color = 0;
    p.depth = 1;
    RayDesc ray;
    ray.Origin = s.position + s.normal * .002;
    ray.Direction = reflect(Normalize(s.position - camera.xyz), s.normal);
    ray.TMin = .001;
    ray.TMax = 250;
    TraceRay(sceneAS, RAY_FLAG_NONE, 255, 0, 1, 0, ray, p);
    float3 f0 = lerp(.04.xxx, s.base, s.metallic);
    return p.color * f0 * (1 - s.roughness * .7);
}
#if !defined(__spirv__) || defined(GALLERY_ENTRY_Miss)
[shader("miss")] void Miss(inout Payload p) {
    p.color = p.depth == 2 ? 1.xxx : Sky(WorldRayDirection());
    p.distance = 250;
    p.instance = 0xffffffff;
    p.uv = 0;
}
#endif
#if !defined(__spirv__) || defined(GALLERY_ENTRY_Closest)
[shader("closesthit")] void Closest(inout Payload p, in BuiltInTriangleIntersectionAttributes attr)
{
    Surface s = HitSurface(attr.barycentrics);
    p.distance = RayTCurrent();
    p.instance = InstanceID();
    p.uv = s.uv;
    p.color = Shade(s, WorldRayOrigin(), Visibility(s));
    if (p.depth == 0 && controls.w)
        p.color += Reflection(s);
    p.color = Diagnostic(s, p.color);
}
#endif
#if !defined(__spirv__) || defined(GALLERY_ENTRY_RayGeneration)
[shader("raygeneration")] void RayGeneration() {
    uint2 pixel = DispatchRaysIndex().xy;
    float3 color;
#if GALLERY_TECHNIQUE == 15
    float4 p = gbuffer[2].Load(int3(pixel, 0));
    if (p.w == 0)
        color = Sky(float3(0, 1, 0));
    else
    {
        Surface s = (Surface)0;
        float4 b = gbuffer[0].Load(int3(pixel, 0)), n = gbuffer[1].Load(int3(pixel, 0)),
               e = gbuffer[3].Load(int3(pixel, 0));
        s.position = p.xyz;
        s.base = b.rgb;
        s.metallic = b.a;
        s.normal = n.xyz;
        s.roughness = n.w;
        s.emission = e.rgb;
        s.ao = e.a;
        s.uv = b.rg;
        Instance inst = GetInstance(uint(p.w - 1));
        s.material = inst.ids.y;
        s.geometry = inst.ids.x;
        color = Shade(s, camera.xyz, Visibility(s));
        if (controls.w)
            color += Reflection(s);
        color = Diagnostic(s, color);
    }
#else
    float2 ndc = (float2(pixel) + .5) / screen.xy * float2(2, -2) + float2(-1, 1);
    float4 target = mul(float4(ndc, 1, 1), inverseViewProjection);
    RayDesc ray;
    ray.Origin = camera.xyz;
    ray.Direction = Normalize(target.xyz / target.w - camera.xyz);
    ray.TMin = .05;
    ray.TMax = 250;
    Payload p;
    p.color = 0;
    p.depth = 0;
    TraceRay(sceneAS, RAY_FLAG_NONE, 255, 0, 1, 0, ray, p);
    color = p.color;
    StoreSurface(pixel, float4(p.uv, p.distance, float(p.instance + 1)));
#endif
    StoreOutput(pixel, float4(color, 1));
}

#endif
