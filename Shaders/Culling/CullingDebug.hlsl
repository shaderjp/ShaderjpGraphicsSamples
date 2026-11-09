#include "Culling.hlsli"
struct Fullscreen {float4 position:SV_Position; VK_LOCATION(0) float2 uv:TEXCOORD0;};
float4 PSMain(Fullscreen i) : SV_Target
{
    uint2 p=min(uint2(i.uv*dimensions.xy),dimensions.xy-1);
    float z;
    if(info.w==0)z=sourceDepth.Load(int3(p,0));
    else if(info.w==2) {
        uint2 small=min(uint2(i.uv*level.zw),level.zw-1);
        z=debugDepth[small.y*level.z+small.x];
    } else {
        uint2 size=dimensions.zw;uint offset=0;
        for(uint m=0;m<info.z && any(size>1);++m){offset+=size.x*size.y;size=max(size/2,1);p/=2;}
        z=debugDepth[offset+p.y*size.x+p.x];
    }
    float gray=pow(saturate(1-z),.25);
    return float4(gray.xxx,1);
}
