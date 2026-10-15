// Independent implementation of normalized dual-quaternion linear blending.
// t98/binding40 is a pair of float4s per joint in the DQS samples.
float4 DqMultiply(float4 a, float4 b)
{
    return float4(a.w*b.xyz + b.w*a.xyz + cross(a.xyz,b.xyz), a.w*b.w-dot(a.xyz,b.xyz));
}
float3 DqRotate(float4 q, float3 v)
{
    return v + 2 * cross(q.xyz, cross(q.xyz,v) + q.w*v);
}
MeshVertex SkinVertexDqs(MeshVertex v, SkinInfluence influence, uint base)
{
    uint reference = 0;
    [unroll] for (uint k=1; k<4; ++k)
        if (influence.weights[k] > influence.weights[reference]) reference=k;
    float4 anchor=skinDualQuaternionParts[2*(base+influence.joints[reference])];
    float4 real=0, dual=0;
    [unroll] for (uint j=0; j<4; ++j)
    {
        uint index=2*(base+influence.joints[j]);
        float4 r=skinDualQuaternionParts[index], d=skinDualQuaternionParts[index+1];
        // q and -q describe the same rotation. Flip both parts into one hemisphere.
        float w=influence.weights[j]*(dot(anchor,r)<0 ? -1 : 1);
        real+=w*r; dual+=w*d;
    }
    float inverseLength=rsqrt(max(dot(real,real),1e-16));
    real*=inverseLength; dual*=inverseLength;
    dual-=real*dot(real,dual);
    float3 translation=2*DqMultiply(dual,float4(-real.xyz,real.w)).xyz;
    v.positionU.xyz=DqRotate(real,v.positionU.xyz)+translation;
    v.normalV.xyz=SafeNormalize(DqRotate(real,v.normalV.xyz));
    float3 tangent=DqRotate(real,v.tangent.xyz);
    v.tangent.xyz=SafeNormalize(tangent-v.normalV.xyz*dot(v.normalV.xyz,tangent));
    return v;
}
