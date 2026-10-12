#include "../Common.hlsli"
VK_BINDING(0,0) cbuffer Scene:register(b0)
{
    row_major float4x4 world,viewProjection,normalMatrix,inverseViewProjection,lightViewProjection;
    float4 baseColorFactor,material,emissiveFactor,cameraPosition,sunDirection,sunColor,environment,shadow,screen;
    uint4 controls; // point count, diagnostic, direct enabled, IBL enabled
};
static const float PI=3.14159265359;
struct Input {VK_LOCATION(0) float3 position:POSITION;VK_LOCATION(1) float2 uv:TEXCOORD0;
    VK_LOCATION(2) float3 normal:NORMAL;VK_LOCATION(3) float4 tangent:TANGENT;};
struct SurfaceVertex {float4 position:SV_Position;VK_LOCATION(0) float2 uv:TEXCOORD0;
    VK_LOCATION(1) float3 worldPosition:TEXCOORD1;VK_LOCATION(2) float3 normal:TEXCOORD2;VK_LOCATION(3) float4 tangent:TEXCOORD3;};
struct Fullscreen {float4 position:SV_Position;VK_LOCATION(0) float2 uv:TEXCOORD0;};
float3 SafeNormalize(float3 v){return v*rsqrt(max(dot(v,v),1e-12));}
float2 SignNotZero(float2 v){return float2(v.x>=0?1:-1,v.y>=0?1:-1);}
float2 EncodeNormal(float3 n){n/=dot(abs(n),1.xxx);return n.z>=0?n.xy:(1-abs(n.yx))*SignNotZero(n.xy);}
float3 DecodeNormal(float2 e){float3 n=float3(e,1-abs(e.x)-abs(e.y));float t=saturate(-n.z);n.xy+=float2(n.x>=0?-t:t,n.y>=0?-t:t);return SafeNormalize(n);}
float3 ReconstructPosition(uint2 pixel,float depth){float2 ndc=(float2(pixel)+.5)/screen.xy*float2(2,-2)+float2(-1,1);float4 p=mul(float4(ndc,depth,1),inverseViewProjection);return p.xyz/p.w;}
float3 EnvironmentDirection(float3 v){float c=cos(environment.y),s=sin(environment.y);return float3(c*v.x+s*v.z,v.y,-s*v.x+c*v.z);}
