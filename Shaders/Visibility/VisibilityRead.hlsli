#include "../Deferred/Lighting.hlsli"
VK_BINDING(13,0) Texture2D<uint2> visibilityTexture : register(t10);
VK_BINDING(17,0) Texture2D<float> visibilityDepth : register(t14);
struct VertexData { float3 position; float2 uv; float3 normal; float4 tangent; };
// Three aligned float4 words preserve the IA's packed 48-byte vertex on both APIs.
// A float3/float2/float3/float4 storage struct otherwise has a 64-byte std430 stride.
struct VertexWords { float4 positionU, vNormal, tangent; };
struct InstanceData { row_major float4x4 world, normalMatrix; uint4 geometry; };
struct MaterialData { float4 base, factors, emission; uint4 images; float4 alpha; };
VK_BINDING(24,0) StructuredBuffer<VertexWords> visibilityVertices : register(t17);
VK_BINDING(25,0) StructuredBuffer<uint> visibilityIndices : register(t18);
VK_BINDING(26,0) StructuredBuffer<InstanceData> visibilityInstances : register(t19);
VK_BINDING(27,0) StructuredBuffer<MaterialData> visibilityMaterials : register(t20);
VK_BINDING(28,0) Texture2D<float4> visibilityTextures[64] : register(t21);
VertexData ReadVisibilityVertex(uint index)
{
    VertexWords w = visibilityVertices[index];
    VertexData v;
    v.position = w.positionU.xyz;
    v.uv = float2(w.positionU.w, w.vNormal.x);
    v.normal = w.vNormal.yzw;
    v.tangent = w.tangent;
    return v;
}
float Cross2(float2 a, float2 b) { return a.x*b.y-a.y*b.x; }
struct Barycentrics { float3 value, dx, dy; };
Barycentrics PerspectiveBarycentrics(float4 a, float4 b, float4 c, uint2 pixel)
{
    // Homogeneous equations remain valid when a vertex is behind the near plane.
    float2 q=(float2(pixel)+.5)/screen.xy*float2(2,-2)+float2(-1,1);
    float2 A=a.xy-q*a.w, B=b.xy-q*b.w, C=c.xy-q*c.w;
    float3 num=float3(Cross2(B,C),Cross2(C,A),Cross2(A,B));
    float2 x=float2(2/screen.x,0), y=float2(0,-2/screen.y);
    float3 nx=float3(Cross2(-x*b.w,C)+Cross2(B,-x*c.w),
                     Cross2(-x*c.w,A)+Cross2(C,-x*a.w),
                     Cross2(-x*a.w,B)+Cross2(A,-x*b.w));
    float3 ny=float3(Cross2(-y*b.w,C)+Cross2(B,-y*c.w),
                     Cross2(-y*c.w,A)+Cross2(C,-y*a.w),
                     Cross2(-y*a.w,B)+Cross2(A,-y*b.w));
    float den=dot(num,1.xxx);
    Barycentrics r;
    r.value=num/den;
    r.dx=(nx-r.value*dot(nx,1.xxx))/den;
    r.dy=(ny-r.value*dot(ny,1.xxx))/den;
    return r;
}
float4 VisibilitySample(uint image,float2 uv,float2 dx,float2 dy)
{
    return visibilityTextures[NonUniformResourceIndex(image)].SampleGrad(materialSampler,uv,dx,dy);
}
float3 VisibilityHash(uint id)
{
    uint h=id*1664525u+1013904223u; h^=h>>16;
    return float3(h&255,(h>>8)&255,(h>>16)&255)/255;
}
float3 ShadeVisibility(uint2 pixel)
{
    if(controls.y==9)
    {
        uint2 xy=min(uint2((float2(pixel)+.5)/screen.xy*shadow.w),uint2(shadow.w-1,shadow.w-1));
        return shadowTexture.Load(int3(xy,0)).xxx;
    }
    uint2 id=visibilityTexture.Load(int3(pixel,0));
    if(id.x==0)return 0;
    uint primitiveId=id.y&0x7fffffff;
    if(controls.y==17)return VisibilityHash(id.x);
    if(controls.y==18)return VisibilityHash(primitiveId+1);
    InstanceData inst=visibilityInstances[id.x-1];
    uint first=inst.geometry.y+primitiveId*3;
    VertexData a=ReadVisibilityVertex(inst.geometry.x+visibilityIndices[first]),
               b=ReadVisibilityVertex(inst.geometry.x+visibilityIndices[first+1]),
               c=ReadVisibilityVertex(inst.geometry.x+visibilityIndices[first+2]);
    float4 ca=mul(mul(float4(a.position,1),inst.world),viewProjection),
           cb=mul(mul(float4(b.position,1),inst.world),viewProjection),
           cc=mul(mul(float4(c.position,1),inst.world),viewProjection);
    Barycentrics bary=PerspectiveBarycentrics(ca,cb,cc,pixel);
    float2 uv=a.uv*bary.value.x+b.uv*bary.value.y+c.uv*bary.value.z;
    float2 dx=a.uv*bary.dx.x+b.uv*bary.dx.y+c.uv*bary.dx.z;
    float2 dy=a.uv*bary.dy.x+b.uv*bary.dy.y+c.uv*bary.dy.z;
    if(controls.y==19)return saturate(bary.value);
    if(controls.y==20)return saturate(float3(length(dx)*32,length(dy)*32,0));
    float depth=visibilityDepth.Load(int3(pixel,0));
    uint cell=CellIndex(pixel,depth);
    if(gridControls.y!=0)
    {
        uint2 header=gridControls.x==0?uint2(controls.x,0):lightHeaders[cell];
        if(gridControls.y==3)return header.y!=0?float3(1,0,0):.04.xxx;
        float t=gridControls.y==2?float(DepthSlice(ViewDepth(depth)))/max(1u,gridSize.z-1):min(float(header.x)/128,1);
        float3 color=saturate(float3(1.5-abs(t*4-3),1.5-abs(t*4-2),1.5-abs(t*4-1)));
        return any(pixel%16==0)?color*.2:color;
    }
    MaterialData m=visibilityMaterials[inst.geometry.z];
    float4 base=VisibilitySample(m.images.x,uv,dx,dy)*m.base;
    float3 orm=VisibilitySample(m.images.z,uv,dx,dy).rgb;
    Surface s; s.base=base.rgb; s.alpha=1;
    s.ao=lerp(1,orm.r,m.factors.w);
    s.metallic=saturate(orm.b*m.factors.x); s.roughness=max(saturate(orm.g*m.factors.y),.045);
    float3 n=a.normal*bary.value.x+b.normal*bary.value.y+c.normal*bary.value.z;
    float4 tangent=a.tangent*bary.value.x+b.tangent*bary.value.y+c.tangent*bary.value.z;
    float3 N=SafeNormalize(mul(float4(n,0),inst.normalMatrix).xyz);
    float3 T=mul(float4(tangent.xyz,0),inst.world).xyz; T=SafeNormalize(T-N*dot(N,T));
    float3 B=tangent.w*cross(N,T);
    float3 nt=VisibilitySample(m.images.y,uv,dx,dy).xyz*2-1; nt.xy*=m.factors.z;
    s.normal=SafeNormalize(T*nt.x+B*nt.y+N*nt.z)*((id.y&0x80000000)==0?1:-1);
    s.emission=VisibilitySample(m.images.w,uv,dx,dy).rgb*m.emission.rgb;
    if(controls.y==1)return s.base; if(controls.y==2)return s.ao.xxx;
    if(controls.y==3)return float3(EncodeNormal(s.normal)*.5+.5,0); if(controls.y==4)return s.normal*.5+.5;
    if(controls.y==5)return s.metallic.xxx; if(controls.y==6)return s.roughness.xxx;
    if(controls.y==7)return s.emission; if(controls.y==8)return depth.xxx;
    return ShadeSurface(s,ReconstructPosition(pixel,depth),cell);
}
