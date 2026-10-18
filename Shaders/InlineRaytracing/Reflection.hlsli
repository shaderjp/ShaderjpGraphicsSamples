// Evaluate the secondary surface directly: no recursive ShadeSurface/Reflection calls.
float3 InlineReflection(Surface source, float3 p, float3 V)
{
    float3 R = SafeNormalize(reflect(-V, source.normal));
    RayDesc ray;
    ray.Origin = InlineOrigin(p, source.normal, R);
    ray.Direction = R;
    ray.TMin = 0;
    ray.TMax = inlineRay.y;
    RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(inlineAS, 0, 255, ray);
    while (q.Proceed())
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            !InlineAlphaRejected(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(),
                                 q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    if (q.CommittedStatus() == COMMITTED_NOTHING)
        return controls.w != 0
                   ? environmentTexture.SampleLevel(environmentSampler, EnvironmentDirection(R), 0).rgb *
                         environment.x
                   : 0;
    InlineInstance inst = inlineInstances[q.CommittedInstanceID()];
    InlinePrimitive prim = inlinePrimitives[inst.primitive.x];
    InlineMaterial m = inlineMaterials[prim.offsets.z];
    uint k = prim.offsets.y + q.CommittedPrimitiveIndex() * 3;
    InlineVertex a = inlineVertices[prim.offsets.x + inlineIndices[k]],
                 b = inlineVertices[prim.offsets.x + inlineIndices[k + 1]],
                 c = inlineVertices[prim.offsets.x + inlineIndices[k + 2]];
    float2 bary = q.CommittedTriangleBarycentrics();
    float3 w = float3(1 - bary.x - bary.y, bary);
    float4 n = a.normalV * w.x + b.normalV * w.y + c.normalV * w.z,
           t = a.tangent * w.x + b.tangent * w.y + c.tangent * w.z;
    float2 uv = float2(dot(float3(a.positionU.w, b.positionU.w, c.positionU.w), w), n.w);
    Surface s = (Surface)0;
    s.base =
        (inlineTextures[NonUniformResourceIndex(m.images.x)].SampleLevel(materialSampler, uv, 0) * m.base)
            .rgb;
    float3 orm = inlineTextures[NonUniformResourceIndex(m.images.z)].SampleLevel(materialSampler, uv, 0).rgb;
    s.ao = lerp(1, orm.r, m.factors.w);
    s.metallic = saturate(orm.b * m.factors.x);
    s.roughness = max(saturate(orm.g * m.factors.y), .045);
    float3 N = SafeNormalize(mul(float4(n.xyz, 0), inst.normalMatrix).xyz),
           T = mul(float4(t.xyz, 0), inst.world).xyz;
    T = SafeNormalize(T - N * dot(N, T));
    float3 B = t.w * cross(N, T);
    float3 nt =
        inlineTextures[NonUniformResourceIndex(m.images.y)].SampleLevel(materialSampler, uv, 0).xyz * 2 - 1;
    nt.xy *= m.factors.z;
    s.normal = SafeNormalize(T * nt.x + B * nt.y + N * nt.z) * (q.CommittedTriangleFrontFace() ? 1 : -1);
    s.emission = inlineTextures[NonUniformResourceIndex(m.images.w)].SampleLevel(materialSampler, uv, 0).rgb *
                 m.emission.rgb;
    float3 hit = ray.Origin + R * q.CommittedRayT(), eye = -R, result = s.emission;
    if (controls.z != 0)
    {
        float visible = inlineControls.x != 0 ? InlineVisibility(hit, s.normal, sunDirection.xyz, inlineRay.y)
                                              : ShadowVisibility(hit);
        result += EvaluateDirect(s, eye, sunDirection.xyz, sunColor.rgb * sunColor.w) * visible;
        for (uint i = 0; i < controls.x; ++i)
        {
            PointLight l = points[i];
            float3 d = l.positionRange.xyz - hit;
            float d2 = max(dot(d, d), 1e-4), att = 1 / d2;
            if (l.positionRange.w > 0)
                att *= pow(saturate(1 - pow(sqrt(d2) / l.positionRange.w, 4)), 2);
            result +=
                EvaluateDirect(s, eye, SafeNormalize(d), l.colorIntensity.rgb * l.colorIntensity.w * att);
        }
    }
    if (controls.w != 0)
    {
        float nv = max(saturate(dot(s.normal, eye)), 1e-5);
        float3 f0 = lerp(.04.xxx, s.base, s.metallic), F = Fresnel(nv, f0);
        result += (1 - F) * (1 - s.metallic) * s.base / PI *
                  irradianceTexture.SampleLevel(environmentSampler, EnvironmentDirection(s.normal), 0).rgb *
                  environment.x * s.ao;
        float2 ab = brdfTexture.SampleLevel(environmentSampler, float2(nv, s.roughness), 0);
        result += specularTexture
                      .SampleLevel(environmentSampler, EnvironmentDirection(reflect(-eye, s.normal)),
                                   s.roughness * 8)
                      .rgb *
                  (f0 * ab.x + ab.y) * environment.x;
    }
    return max(result, 0);
}
