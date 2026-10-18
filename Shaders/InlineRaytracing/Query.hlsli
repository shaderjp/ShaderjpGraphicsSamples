// Included after Deferred/Resources.hlsli. Bindings do not overlap the light grid.
VK_BINDING(50, 0) cbuffer InlineSettings : register(b2)
{
    uint4 inlineControls;
    float4 inlineRay;
};
VK_BINDING(51, 0) RaytracingAccelerationStructure inlineAS : register(t100);
struct InlineVertex
{
    float4 positionU, normalV, tangent;
};
struct InlinePrimitive
{
    uint4 offsets;
};
struct InlineInstance
{
    row_major float4x4 world, normalMatrix;
    uint4 primitive;
};
struct InlineMaterial
{
    float4 base, factors, emission;
    uint4 images;
    float4 alpha;
};
VK_BINDING(52, 0) StructuredBuffer<InlineVertex> inlineVertices : register(t101);
VK_BINDING(53, 0) StructuredBuffer<uint> inlineIndices : register(t102);
VK_BINDING(54, 0) StructuredBuffer<InlinePrimitive> inlinePrimitives : register(t103);
VK_BINDING(55, 0) StructuredBuffer<InlineInstance> inlineInstances : register(t104);
VK_BINDING(56, 0) StructuredBuffer<InlineMaterial> inlineMaterials : register(t105);
VK_BINDING(57, 0) Texture2D<float4> inlineTextures[64] : register(t106);
static uint inlineRayCount = 0;
bool InlineAlphaRejected(uint instanceId, uint primitiveId, float2 bary)
{
    InlinePrimitive p = inlinePrimitives[inlineInstances[instanceId].primitive.x];
    InlineMaterial m = inlineMaterials[p.offsets.z];
    uint k = p.offsets.y + primitiveId * 3;
    InlineVertex a = inlineVertices[p.offsets.x + inlineIndices[k]],
                 b = inlineVertices[p.offsets.x + inlineIndices[k + 1]],
                 c = inlineVertices[p.offsets.x + inlineIndices[k + 2]];
    float3 w = float3(1 - bary.x - bary.y, bary);
    float2 uv = float2(dot(float3(a.positionU.w, b.positionU.w, c.positionU.w), w),
                       dot(float3(a.normalV.w, b.normalV.w, c.normalV.w), w));
    return inlineTextures[NonUniformResourceIndex(m.images.x)].SampleLevel(materialSampler, uv, 0).a *
               m.base.a <
           m.alpha.x;
}
float3 InlineOrigin(float3 p, float3 normal, float3 direction)
{
    return p + normal * (dot(normal, direction) >= 0 ? inlineRay.x : -inlineRay.x);
}
float InlineVisibility(float3 p, float3 normal, float3 direction, float distance)
{
    if (shadow.x == 0)
        return 1;
    if (inlineControls.w == 2)
        ++inlineRayCount;
    RayDesc ray;
    ray.Origin = InlineOrigin(p, normal, direction);
    ray.Direction = direction;
    ray.TMin = 0;
    ray.TMax = max(distance - inlineRay.x, inlineRay.x);
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
             RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>
        q;
    q.TraceRayInline(inlineAS, 0, 255, ray);
    while (q.Proceed())
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            !InlineAlphaRejected(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(),
                                 q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    return q.CommittedStatus() == COMMITTED_NOTHING ? 1 : 0;
}
float InlinePointVisibility(float3 p, float3 normal, float3 L, float distance, uint light)
{
    return light < inlineControls.y ? InlineVisibility(p, normal, L, distance) : 1;
}
#if INLINE_SAMPLE == 27
float3 InlineReflection(Surface s, float3 p, float3 V);
#endif
