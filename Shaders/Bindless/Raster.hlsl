#include "Scene.hlsli"
struct Interpolated
{
    float4 position : SV_Position;
    VK_LOCATION(0) float2 uv : TEXCOORD0;
    VK_LOCATION(1) float3 world : TEXCOORD1;
    VK_LOCATION(2) float3 normal : TEXCOORD2;
    VK_LOCATION(3) float4 tangent : TEXCOORD3;
    VK_LOCATION(4) nointerpolation uint instance : TEXCOORD4;
};
Interpolated Transform(uint instance, uint index)
{
    Instance inst = GetInstance(instance);
    Vertex v = GetVertex(GetGeometry(inst.ids.x), index);
    Interpolated o;
    float4 p = mul(float4(v.positionU.xyz, 1), inst.world);
    o.position = mul(p, viewProjection);
    o.world = p.xyz;
    o.normal = mul(float4(v.normalV.xyz, 0), inst.normal).xyz;
    o.tangent = float4(mul(float4(v.tangent.xyz, 0), inst.world).xyz, v.tangent.w);
    o.uv = float2(v.positionU.w, v.normalV.w);
    o.instance = instance;
    return o;
}
Interpolated VSMain(uint vertex : SV_VertexID)
{
    return Transform(drawInstance, vertex);
}
[numthreads(64, 1, 1)][outputtopology("triangle")] void MSMain(uint3 group : SV_GroupID, uint thread : SV_GroupIndex,
                                                               out vertices Interpolated vertices[64],
                                                               out indices uint3 triangles[96]) {
    uint instance = drawInstance, id = drawBase + group.x;
    bool emit = true;
    if (drawDriven)
    {
        uint work = group.x + group.y * 256;
        emit = work < VisibleLoad(0);
        if (emit)
        {
            instance = VisibleLoad(64 + work * 8);
            id = VisibleLoad(68 + work * 8);
        }
        else
        {
            instance = 0;
            id = 0;
        }
    }
    Instance inst = GetInstance(instance);
    Geometry g = GetGeometry(inst.ids.x);
    Meshlet m = GetMeshlet(g, id);
    emit = emit && (drawDriven || Visible(inst, m));
    SetMeshOutputCounts(emit ? m.offsets.z : 0, emit ? m.offsets.w : 0);
    if (!emit)
        return;
    if (thread < m.offsets.z)
        vertices[thread] = Transform(instance, MeshVertexIndex(g, m.offsets.x + thread));
    for (uint t = thread; t < m.offsets.w; t += 64)
        triangles[t] = uint3(MeshTriangleIndex(g, m.offsets.y + t * 3), MeshTriangleIndex(g, m.offsets.y + t * 3 + 1),
                             MeshTriangleIndex(g, m.offsets.y + t * 3 + 2));
}
struct GBuffer
{
    float4 base : SV_Target0;
    float4 normal : SV_Target1;
    float4 position : SV_Target2;
    float4 emission : SV_Target3;
};
GBuffer PSMain(Interpolated i, bool front : SV_IsFrontFace)
{
    Instance inst = GetInstance(i.instance);
    Material m = GetMaterial(inst.ids.y);
    float2 dx = ddx(i.uv), dy = ddy(i.uv);
    float4 base = SampleImage(m, 0, i.uv, dx, dy, false) * m.base;
    if (m.alpha.y != 0)
        clip(base.a - m.alpha.x);
    float3 orm = SampleImage(m, 2, i.uv, dx, dy, false).rgb, N = Normalize(i.normal),
           T = Normalize(i.tangent.xyz - N * dot(N, i.tangent.xyz));
    float3 nt = SampleImage(m, 1, i.uv, dx, dy, false).xyz * 2 - 1;
    nt.xy *= m.factors.z;
    N = Normalize(T * nt.x + i.tangent.w * cross(N, T) * nt.y + N * nt.z) * (front ? 1 : -1);
    GBuffer o;
    o.base = float4(controls.y == 3 ? float3(frac(i.uv), 0) : base.rgb, saturate(orm.b * m.factors.x));
    o.normal = float4(N, max(.045, saturate(orm.g * m.factors.y)));
    // Integer IDs are exactly representable in the float32 position target.
    o.position = float4(i.world, float(i.instance + 1));
    o.emission = float4(SampleImage(m, 3, i.uv, dx, dy, false).rgb * m.emission.rgb, lerp(1, orm.r, m.factors.w));
    return o;
}
