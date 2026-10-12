#include "../Common.hlsli"
VK_BINDING(0,0) cbuffer Scene : register(b0)
{
    row_major float4x4 world,viewProjection,normalMatrix;
    float4 baseColorFactor,cameraPosition,lightDirection,lightColor,ambientExposure,material,emissiveFactor,shading;
    row_major float4x4 inverseViewProjection,lightViewProjection;
    float4 environment,shadow;
};
static const float PI=3.14159265359;
struct Input {VK_LOCATION(0) float3 position:POSITION;VK_LOCATION(1) float2 uv:TEXCOORD0;
    VK_LOCATION(2) float3 normal:NORMAL;VK_LOCATION(3) float4 tangent:TANGENT;};
struct Output {float4 position:SV_Position;VK_LOCATION(0) float2 uv:TEXCOORD0;
    VK_LOCATION(1) float3 worldPosition:TEXCOORD1;VK_LOCATION(2) float3 normal:TEXCOORD2;VK_LOCATION(3) float4 tangent:TEXCOORD3;};
float3 SafeNormalize(float3 v){return v*rsqrt(max(dot(v,v),1e-12));}
float3 ToneMap(float3 c){
#if LINEAR_HDR
return max(c,0);
#else
c=max(c*ambientExposure.w,0);return c/(1+c);
#endif
}
float3 EnvironmentDirection(float3 v){float c=cos(environment.y),s=sin(environment.y);return float3(c*v.x+s*v.z,v.y,-s*v.x+c*v.z);}
