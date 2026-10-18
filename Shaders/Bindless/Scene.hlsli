#ifndef GALLERY_SCENE
#define GALLERY_SCENE
#include "../Common.hlsli"
struct Vertex
{
    float4 positionU, normalV, tangent;
};
struct Geometry
{
    uint4 offsets, lengths, resources, extra;
};
struct Material
{
    float4 base, factors, emission;
    uint4 images, layers;
    float4 alpha;
};
struct Instance
{
    row_major float4x4 world, normal;
    uint4 ids;
};
struct Meshlet
{
    uint4 offsets;
    float4 sphere, cone, apex;
};
VK_BINDING(0, 0) cbuffer Frame : register(b0)
{
    row_major float4x4 viewProjection, inverseViewProjection;
    float4 camera, sunDirection, sunColor, screen, planes[6];
    uint4 controls, heaps;
};
#ifdef __spirv__
struct DrawData
{
    uint instance, base, driven, candidateCount;
};
[[vk::push_constant]] ConstantBuffer<DrawData> draw;
#define drawInstance draw.instance
#define drawBase draw.base
#define drawDriven draw.driven
#define candidateCount draw.candidateCount
#else
cbuffer Draw : register(b1)
{
    uint drawInstance, drawBase, drawDriven, candidateCount;
};
#endif
#if GALLERY_BINDING == 0 && GALLERY_TECHNIQUE >= 14
VK_BINDING(1, 0) Texture2DArray<float4> images[4] : register(t0);
#elif GALLERY_BINDING == 0
VK_BINDING(1, 0) Texture2D<float4> images[4] : register(t0);
#elif defined(__spirv__)
VK_BINDING(1, 0) Texture2D<float4> images[] : register(t0);
#endif
VK_BINDING(2, 0) SamplerState linearSampler : register(s0);
#if GALLERY_BINDING == 2
#ifdef __spirv__
VK_BINDING(3, 0) ByteAddressBuffer buffers[] : register(t512);
#endif
#else
VK_BINDING(3, 0) ByteAddressBuffer buffers[9] : register(t512);
#endif
#ifdef __spirv__
[[vk::image_format("rgba32f")]]
#endif
VK_BINDING(4, 0) RWTexture2D<float4> outputs[5] : register(u0);
VK_BINDING(5, 0) Texture2D<float4> gbuffer[4] : register(t900);
VK_BINDING(6, 0) RaytracingAccelerationStructure sceneAS : register(t904);
VK_BINDING(7, 0) RWByteAddressBuffer visibility : register(u5);

uint4 Load4(uint view, uint offset)
{
#if GALLERY_BINDING == 2 && !defined(__spirv__)
    ByteAddressBuffer b = ResourceDescriptorHeap[NonUniformResourceIndex(heaps.y + view)];
    return b.Load4(offset);
#elif GALLERY_BINDING == 2
    return buffers[NonUniformResourceIndex(view)].Load4(offset);
#else
    return buffers[view].Load4(offset);
#endif
}
uint Load1(uint view, uint offset)
{
#if GALLERY_BINDING == 2 && !defined(__spirv__)
    ByteAddressBuffer b = ResourceDescriptorHeap[NonUniformResourceIndex(heaps.y + view)];
    return b.Load(offset);
#elif GALLERY_BINDING == 2
    return buffers[NonUniformResourceIndex(view)].Load(offset);
#else
    return buffers[view].Load(offset);
#endif
}
Geometry GetGeometry(uint id)
{
    Geometry g;
    uint a = id * 64;
    g.offsets = Load4(0, a);
    g.lengths = Load4(0, a + 16);
    g.resources = Load4(0, a + 32);
    g.extra = Load4(0, a + 48);
    return g;
}
Material GetMaterial(uint id)
{
    Material m;
    uint a = id * 96;
    m.base = asfloat(Load4(1, a));
    m.factors = asfloat(Load4(1, a + 16));
    m.emission = asfloat(Load4(1, a + 32));
    m.images = Load4(1, a + 48);
    m.layers = Load4(1, a + 64);
    m.alpha = asfloat(Load4(1, a + 80));
    return m;
}
Instance GetInstance(uint id)
{
    Instance i;
    uint a = id * 144;
    i.world =
        float4x4(asfloat(Load4(2, a)), asfloat(Load4(2, a + 16)), asfloat(Load4(2, a + 32)), asfloat(Load4(2, a + 48)));
    i.normal = float4x4(asfloat(Load4(2, a + 64)), asfloat(Load4(2, a + 80)), asfloat(Load4(2, a + 96)),
                        asfloat(Load4(2, a + 112)));
    i.ids = Load4(2, a + 128);
    return i;
}
Vertex GetVertex(Geometry g, uint id)
{
    Vertex v;
#if GALLERY_BINDING == 2
    uint view = g.resources.x, a = id * 48;
#elif GALLERY_TECHNIQUE == 13
    uint view = 3, a = (drawDriven ? g.offsets.x + id : id) * 48;
#else
    uint view = 3, a = (g.offsets.x + id) * 48;
#endif
    v.positionU = asfloat(Load4(view, a));
    v.normalV = asfloat(Load4(view, a + 16));
    v.tangent = asfloat(Load4(view, a + 32));
    return v;
}
uint GetIndex(Geometry g, uint id)
{
#if GALLERY_BINDING == 2
    return Load1(g.resources.y, id * 4);
#elif GALLERY_TECHNIQUE == 13
    return Load1(4, (drawDriven ? g.offsets.y + id : id) * 4);
#else
    return Load1(4, (g.offsets.y + id) * 4);
#endif
}
Meshlet GetMeshlet(Geometry g, uint id)
{
    Meshlet m;
#if GALLERY_BINDING == 2
    uint view = g.resources.z, a = id * 64;
#elif GALLERY_TECHNIQUE == 13
    uint view = 5, a = (drawDriven ? g.offsets.z + id : id) * 64;
#else
    uint view = 5, a = (g.offsets.z + id) * 64;
#endif
    m.offsets = Load4(view, a);
    m.sphere = asfloat(Load4(view, a + 16));
    m.cone = asfloat(Load4(view, a + 32));
    m.apex = asfloat(Load4(view, a + 48));
    return m;
}
uint MeshVertexIndex(Geometry g, uint id)
{
#if GALLERY_BINDING == 2
    return Load1(g.resources.w, id * 4);
#elif GALLERY_TECHNIQUE == 13
    return Load1(6, (drawDriven ? g.lengths.z + id : id) * 4);
#else
    return Load1(6, (g.lengths.z + id) * 4);
#endif
}
uint MeshTriangleIndex(Geometry g, uint id)
{
#if GALLERY_BINDING == 2
    return Load1(g.extra.x, id * 4);
#elif GALLERY_TECHNIQUE == 13
    return Load1(7, (drawDriven ? g.lengths.w + id : id) * 4);
#else
    return Load1(7, (g.lengths.w + id) * 4);
#endif
}
float4 SampleImage(Material m, uint channel, float2 uv, float2 dx, float2 dy, bool ray)
{
#if GALLERY_BINDING == 0 && GALLERY_TECHNIQUE >= 14
    float3 q = float3(uv, m.layers[channel]);
    if (channel == 0)
        return images[0].SampleLevel(linearSampler, q, 0);
    if (channel == 1)
        return images[1].SampleLevel(linearSampler, q, 0);
    if (channel == 2)
        return images[2].SampleLevel(linearSampler, q, 0);
    return images[3].SampleLevel(linearSampler, q, 0);
#elif GALLERY_BINDING == 0
    // Static descriptor indices: this mode needs no descriptor indexing in the material shader.
    if (channel == 0)
        return images[0].SampleGrad(linearSampler, uv, dx, dy);
    if (channel == 1)
        return images[1].SampleGrad(linearSampler, uv, dx, dy);
    if (channel == 2)
        return images[2].SampleGrad(linearSampler, uv, dx, dy);
    return images[3].SampleGrad(linearSampler, uv, dx, dy);
#else
#ifdef __spirv__
    Texture2D<float4> image = images[NonUniformResourceIndex(m.images[channel])];
#else
    Texture2D<float4> image = ResourceDescriptorHeap[NonUniformResourceIndex(heaps.x + m.images[channel])];
#endif
#if GALLERY_TECHNIQUE >= 14
    return image.SampleLevel(linearSampler, uv, 0);
#else
    return ray ? image.SampleLevel(linearSampler, uv, 0) : image.SampleGrad(linearSampler, uv, dx, dy);
#endif
#endif
}
float3 Normalize(float3 v)
{
    return v * rsqrt(max(dot(v, v), 1e-15));
}
float3 Sky(float3 d)
{
    return lerp(float3(.12, .15, .19), float3(.38, .51, .7), saturate(d.y * .5 + .5));
}
float3 IdColor(uint id)
{
    uint h = id * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28) + 4)) ^ h) * 277803737u;
    return .15 + .85 * float3(h & 255, (h >> 8) & 255, (h >> 16) & 255) / 255;
}
struct Surface
{
    float3 position, normal, base, emission;
    float metallic, roughness, ao;
    uint material, geometry;
    float2 uv;
};
Surface ReadSurface(uint instId, uint3 ix, float3 weights, bool front, float2 dx, float2 dy, bool ray)
{
    Instance inst = GetInstance(instId);
    Geometry g = GetGeometry(inst.ids.x);
    Material m = GetMaterial(inst.ids.y);
    Vertex a = GetVertex(g, ix.x), b = GetVertex(g, ix.y), c = GetVertex(g, ix.z);
    float4 p = a.positionU * weights.x + b.positionU * weights.y + c.positionU * weights.z;
    float4 n = a.normalV * weights.x + b.normalV * weights.y + c.normalV * weights.z;
    float4 tangent = a.tangent * weights.x + b.tangent * weights.y + c.tangent * weights.z;
    Surface s;
    s.position = mul(float4(p.xyz, 1), inst.world).xyz;
    s.uv = float2(p.w, n.w);
    s.material = inst.ids.y;
    s.geometry = inst.ids.x;
    float4 base = SampleImage(m, 0, s.uv, dx, dy, ray) * m.base;
    s.base = base.rgb;
    float3 orm = SampleImage(m, 2, s.uv, dx, dy, ray).rgb;
    s.metallic = saturate(orm.b * m.factors.x);
    s.roughness = max(.045, saturate(orm.g * m.factors.y));
    s.ao = lerp(1, orm.r, m.factors.w);
    float3 N = Normalize(mul(float4(n.xyz, 0), inst.normal).xyz), T = mul(float4(tangent.xyz, 0), inst.world).xyz;
    T = Normalize(T - N * dot(N, T));
    float3 nt = SampleImage(m, 1, s.uv, dx, dy, ray).xyz * 2 - 1;
    nt.xy *= m.factors.z;
    s.normal = Normalize(T * nt.x + tangent.w * cross(N, T) * nt.y + N * nt.z) * (front ? 1 : -1);
    s.emission = SampleImage(m, 3, s.uv, dx, dy, ray).rgb * m.emission.rgb;
    return s;
}
float3 Shade(Surface s, float3 eye, float shadow)
{
    const float PI = 3.14159265359;
    float3 N = s.normal, V = Normalize(eye - s.position), L = Normalize(-sunDirection.xyz), H = Normalize(V + L);
    float nv = max(.0001, dot(N, V)), nl = saturate(dot(N, L)), nh = saturate(dot(N, H)), vh = saturate(dot(V, H));
    float a = s.roughness * s.roughness, a2 = a * a, q = nh * nh * (a2 - 1) + 1, D = a2 / max(PI * q * q, 1e-6);
    float k = (s.roughness + 1) * (s.roughness + 1) / 8, G = nv / (nv * (1 - k) + k) * nl / (nl * (1 - k) + k);
    float3 f0 = lerp(.04.xxx, s.base, s.metallic), F = f0 + (1 - f0) * pow(1 - vh, 5);
    float3 direct =
        ((1 - F) * (1 - s.metallic) * s.base / PI + D * G * F / max(4 * nv * nl, 1e-5)) * sunColor.rgb * nl * shadow;
    return direct + s.base * (1 - s.metallic) * (.08 + .12 * saturate(N.y)) * s.ao + s.emission;
}
float3 Diagnostic(Surface s, float3 color)
{
    return controls.y == 1   ? IdColor(s.material)
           : controls.y == 2 ? IdColor(s.geometry)
           : controls.y == 3 ? float3(frac(s.uv), 0)
                             : color;
}
bool Visible(Instance inst, Meshlet m)
{
    if (!heaps.w)
        return true;
    float3 p = mul(float4(m.sphere.xyz, 1), inst.world).xyz;
    float radius = m.sphere.w * length(inst.world[0].xyz);
    for (uint i = 0; i < 6; ++i)
        if (dot(planes[i], float4(p, 1)) < -radius - .0001)
            return false;
    return true;
}
uint VisibleLoad(uint offset)
{
#if GALLERY_BINDING == 2 && !defined(__spirv__)
    RWByteAddressBuffer v = ResourceDescriptorHeap[heaps.z + 5];
    return v.Load(offset);
#else
    return visibility.Load(offset);
#endif
}
void StoreOutput(uint2 pixel, float4 value)
{
#if GALLERY_BINDING == 2 && !defined(__spirv__)
    RWTexture2D<float4> image = ResourceDescriptorHeap[heaps.z];
    image[pixel] = value;
#elif GALLERY_BINDING == 2
    outputs[NonUniformResourceIndex(heaps.z)][pixel] = value;
#else
    outputs[0][pixel] = value;
#endif
}
void StoreSurface(uint2 pixel, float4 value)
{
#if GALLERY_BINDING == 2 && !defined(__spirv__)
    RWTexture2D<float4> image = ResourceDescriptorHeap[heaps.z + 1];
    image[pixel] = value;
#elif GALLERY_BINDING == 2
    outputs[NonUniformResourceIndex(heaps.z + 1)][pixel] = value;
#else
    outputs[1][pixel] = value;
#endif
}
#endif
