#include "Scene.hlsli"
// The flags, hit shading and composition deliberately match Raytracing.hlsl.
bool InlineReject(uint id, uint primitiveId, float2 bary)
{
    Instance inst = GetInstance(id);
    Material m = GetMaterial(inst.ids.y);
    if (m.alpha.y == 0)
        return false;
    Geometry g = GetGeometry(inst.ids.x);
    uint k = primitiveId * 3;
    Vertex a = GetVertex(g, GetIndex(g, k)), b = GetVertex(g, GetIndex(g, k + 1)),
           c = GetVertex(g, GetIndex(g, k + 2));
    float3 w = float3(1 - bary.x - bary.y, bary);
    float2 uv = float2(dot(float3(a.positionU.w, b.positionU.w, c.positionU.w), w),
                       dot(float3(a.normalV.w, b.normalV.w, c.normalV.w), w));
    return SampleImage(m, 0, uv, 0, 0, true).a * m.base.a < m.alpha.x;
}
float InlineVisibility(Surface s)
{
    if (!controls.z)
        return 1;
    RayDesc ray;
    ray.Origin = s.position + s.normal * .002;
    ray.Direction = Normalize(-sunDirection.xyz);
    ray.TMin = .001;
    ray.TMax = 250;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    q.TraceRayInline(sceneAS, 0, 255, ray);
    while (q.Proceed())
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            !InlineReject(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(),
                          q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    return q.CommittedStatus() == COMMITTED_NOTHING ? 1 : 0;
}
float3 InlineReflection(Surface s)
{
    RayDesc ray;
    ray.Origin = s.position + s.normal * .002;
    ray.Direction = reflect(Normalize(s.position - camera.xyz), s.normal);
    ray.TMin = .001;
    ray.TMax = 250;
    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(sceneAS, 0, 255, ray);
    while (q.Proceed())
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            !InlineReject(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(),
                          q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    float3 incoming;
    if (q.CommittedStatus() == COMMITTED_NOTHING)
        incoming = Sky(ray.Direction);
    else
    {
        Instance inst = GetInstance(q.CommittedInstanceID());
        Geometry g = GetGeometry(inst.ids.x);
        uint k = q.CommittedPrimitiveIndex() * 3;
        uint3 ix = uint3(GetIndex(g, k), GetIndex(g, k + 1), GetIndex(g, k + 2));
        float2 bary = q.CommittedTriangleBarycentrics();
        Surface hit = ReadSurface(q.CommittedInstanceID(), ix, float3(1 - bary.x - bary.y, bary),
                                  q.CommittedTriangleFrontFace(), 0, 0, true);
        incoming = Diagnostic(hit, Shade(hit, ray.Origin, InlineVisibility(hit)));
    }
    return incoming * lerp(.04.xxx, s.base, s.metallic) * (1 - s.roughness * .7);
}
[numthreads(8, 8, 1)] void CSMain(uint3 id : SV_DispatchThreadID) {
    uint2 pixel = id.xy;
    if (any(pixel >= uint2(screen.xy)))
        return;
    float4 p = gbuffer[2].Load(int3(pixel, 0));
    float3 color;
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
        color = Shade(s, camera.xyz, InlineVisibility(s));
        if (controls.w)
            color += InlineReflection(s);
        color = Diagnostic(s, color);
    }
    StoreOutput(pixel, float4(color, 1));
}
