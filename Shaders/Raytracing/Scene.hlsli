#ifndef RT_SCENE
#define RT_SCENE
#include "../Common.hlsli"
VK_BINDING(0, 0) cbuffer Scene : register(b0)
{
    row_major float4x4 inverseViewProjection, viewProjection;
    float4 camera, sunDirection, sunColor, environment, screen;
    uint4 controls, lighting;
};
VK_BINDING(1, 0) Texture2D<float4> textures[64] : register(t0);
VK_BINDING(2, 0) SamplerState materialSampler : register(s0);
VK_BINDING(3, 0) RaytracingAccelerationStructure sceneAS : register(t64);
struct Vertex
{
    float4 positionU, normalV, tangent;
};
struct Primitive
{
    uint4 offsets;
};
struct Material
{
    float4 base, factors, emission;
    uint4 images;
    float4 alpha;
};
struct Instance
{
    row_major float4x4 world, normalMatrix;
    uint4 primitive;
};
struct PointLight
{
    float4 positionRange, colorIntensity;
};
VK_BINDING(4, 0) StructuredBuffer<Vertex> vertices : register(t65);
VK_BINDING(5, 0) StructuredBuffer<uint> indices : register(t66);
VK_BINDING(6, 0) StructuredBuffer<Primitive> primitives : register(t67);
VK_BINDING(7, 0) StructuredBuffer<Instance> instances : register(t68);
VK_BINDING(8, 0) StructuredBuffer<Material> materials : register(t69);
VK_BINDING(9, 0) StructuredBuffer<PointLight> points : register(t70);
VK_BINDING(10, 0) TextureCube<float4> environmentTexture : register(t71);
VK_BINDING(11, 0) TextureCube<float4> irradianceTexture : register(t72);
VK_BINDING(12, 0) TextureCube<float4> specularTexture : register(t73);
VK_BINDING(13, 0) Texture2D<float2> brdfTexture : register(t74);
VK_BINDING(14, 0) SamplerState environmentSampler : register(s1);
#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
VK_BINDING(15, 0) RWTexture2D<float4> outputImage : register(u0);
VK_BINDING(16, 0) RWTexture2D<float4> surfaceImage : register(u1);    // UV, distance, material ID
VK_BINDING(17, 0) RWTexture2D<float4> reflectionImage : register(u2); // incoming radiance, sun visibility
struct Payload
{
    float3 color;
    uint bounce;
};
static const float PI = 3.14159265359;
static const uint RayStride = RT_TECHNIQUE >= 2 ? 2 : 1;
float3 SafeNormalize(float3 v)
{
    return v * rsqrt(max(dot(v, v), 1e-12));
}
float3 EnvDirection(float3 v)
{
    float c = cos(environment.y), s = sin(environment.y);
    return float3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}
struct HitSurface
{
    float3 position, normal, geometric, base, emission;
    float2 uv;
    float ao, metallic, roughness;
    uint material;
};
HitSurface ReadSurface(float2 bary)
{
    Instance inst = instances[InstanceID()];
    Primitive prim = primitives[inst.primitive.x];
    Material m = materials[prim.offsets.z];
    uint index = prim.offsets.y + PrimitiveIndex() * 3;
    Vertex a = vertices[prim.offsets.x + indices[index]], b = vertices[prim.offsets.x + indices[index + 1]],
           c = vertices[prim.offsets.x + indices[index + 2]];
    float3 weights = float3(1 - bary.x - bary.y, bary);
    float4 p = a.positionU * weights.x + b.positionU * weights.y + c.positionU * weights.z;
    float4 n = a.normalV * weights.x + b.normalV * weights.y + c.normalV * weights.z;
    float4 tangent = a.tangent * weights.x + b.tangent * weights.y + c.tangent * weights.z;
    HitSurface s = (HitSurface)0;
    s.uv = float2(p.w, n.w);
    s.material = prim.offsets.z;
    s.position = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    s.base = (textures[NonUniformResourceIndex(m.images.x)].SampleLevel(materialSampler, s.uv, 0) * m.base).rgb;
    float3 N = SafeNormalize(mul(float4(n.xyz, 0), inst.normalMatrix).xyz);
    s.geometric = SafeNormalize(
        mul(float4(cross(b.positionU.xyz - a.positionU.xyz, c.positionU.xyz - a.positionU.xyz), 0), inst.normalMatrix)
            .xyz);
    if (dot(s.geometric, WorldRayDirection()) > 0)
        s.geometric = -s.geometric;
    s.normal = N * (HitKind() == HIT_KIND_TRIANGLE_BACK_FACE ? -1 : 1);
    s.ao = 1;
    s.roughness = .045;
#if RT_TECHNIQUE >= 2
    float3 orm = textures[NonUniformResourceIndex(m.images.z)].SampleLevel(materialSampler, s.uv, 0).rgb;
    s.ao = lerp(1, orm.r, m.factors.w);
    s.metallic = saturate(orm.b * m.factors.x);
    s.roughness = max(saturate(orm.g * m.factors.y), .045);
    float3 T = mul(float4(tangent.xyz, 0), inst.world).xyz;
    T = SafeNormalize(T - N * dot(N, T));
    float3 B = tangent.w * cross(N, T);
    float3 nt = textures[NonUniformResourceIndex(m.images.y)].SampleLevel(materialSampler, s.uv, 0).xyz * 2 - 1;
    nt.xy *= m.factors.z;
    s.normal = SafeNormalize(T * nt.x + B * nt.y + N * nt.z) * (HitKind() == HIT_KIND_TRIANGLE_BACK_FACE ? -1 : 1);
    s.emission =
        textures[NonUniformResourceIndex(m.images.w)].SampleLevel(materialSampler, s.uv, 0).rgb * m.emission.rgb;
#endif
    return s;
}
bool AlphaRejected(float2 bary)
{
    Instance inst = instances[InstanceID()];
    Primitive p = primitives[inst.primitive.x];
    Material m = materials[p.offsets.z];
    if (m.alpha.y == 0)
        return false;
    uint index = p.offsets.y + PrimitiveIndex() * 3;
    Vertex a = vertices[p.offsets.x + indices[index]], b = vertices[p.offsets.x + indices[index + 1]],
           c = vertices[p.offsets.x + indices[index + 2]];
    float3 weights = float3(1 - bary.x - bary.y, bary);
    float2 uv = float2(dot(float3(a.positionU.w, b.positionU.w, c.positionU.w), weights),
                       dot(float3(a.normalV.w, b.normalV.w, c.normalV.w), weights));
    return textures[NonUniformResourceIndex(m.images.x)].SampleLevel(materialSampler, uv, 0).a * m.base.a < m.alpha.x;
}
float3 OffsetOrigin(HitSurface s, float3 direction)
{
    return s.position + s.geometric * (dot(s.geometric, direction) >= 0 ? screen.w : -screen.w);
}
#endif
