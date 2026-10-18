#include "Scene.hlsli"
[numthreads(64, 1, 1)] void CountReset(uint thread : SV_GroupIndex)
{
    for (uint word = thread; word < 128; word += 64)
        visibility.Store(32 + word * 4, 0);
}
[numthreads(8, 8, 1)] void CountVisible(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid.xy >= uint2(screen.xy))) return;
#if GALLERY_TECHNIQUE == 14
    uint instance = uint(outputs[1][tid.xy].w);
#else
    uint instance = uint(gbuffer[2].Load(int3(tid.xy, 0)).w);
#endif
    if (instance > 0 && instance <= controls.x)
    {
        uint unused;
        visibility.InterlockedOr(32 + ((instance - 1) / 32) * 4, 1u << ((instance - 1) % 32), unused);
    }
}
[numthreads(1, 1, 1)] void Reset() {
    visibility.Store(0, 0);
    visibility.Store3(16, uint3(1, 1, 1));
}
[numthreads(64, 1, 1)] void Cull(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= candidateCount)
        return;
    // Candidate list is a fixed input. GPU results are never read by the CPU before dispatch.
#if GALLERY_BINDING == 2
    uint view = 323;
#else
    uint view = 8;
#endif
    uint2 candidate = uint2(Load1(view, tid.x * 8), Load1(view, tid.x * 8 + 4));
    Instance inst = GetInstance(candidate.x);
    Geometry g = GetGeometry(inst.ids.x);
    if (Visible(inst, GetMeshlet(g, candidate.y)))
    {
        uint index;
        visibility.InterlockedAdd(0, 1, index);
        visibility.Store2(64 + index * 8, candidate);
    }
}
[numthreads(1, 1, 1)] void Finalize() {
    uint count = visibility.Load(0);
    visibility.Store3(16, uint3(min(max(count, 1), 256), (count + 255) / 256, 1));
    if (!count)
        visibility.Store3(16, uint3(1, 1, 1));
}
Surface FromGBuffer(uint2 pixel)
{
    float4 p = gbuffer[2].Load(int3(pixel, 0)), b = gbuffer[0].Load(int3(pixel, 0)),
           n = gbuffer[1].Load(int3(pixel, 0)), e = gbuffer[3].Load(int3(pixel, 0));
    Surface s = (Surface)0;
    s.position = p.xyz;
    s.base = b.rgb;
    s.metallic = b.a;
    s.normal = n.xyz;
    s.roughness = n.w;
    s.emission = e.rgb;
    s.ao = e.a;
    Instance inst = GetInstance(uint(max(p.w - 1, 0)));
    s.material = inst.ids.y;
    s.geometry = inst.ids.x;
    s.uv = b.rg;
    return s;
}
[numthreads(8, 8, 1)] void Light(uint3 tid : SV_DispatchThreadID) {
    if (any(tid.xy >= uint2(screen.xy)))
        return;
    float4 p = gbuffer[2].Load(int3(tid.xy, 0));
    if (p.w == 0)
    {
        StoreOutput(tid.xy, float4(Sky(float3(0, 1, 0)), 1));
        return;
    }
    Surface s = FromGBuffer(tid.xy);
    StoreOutput(tid.xy, float4(Diagnostic(s, Shade(s, camera.xyz, 1)), 1));
}
