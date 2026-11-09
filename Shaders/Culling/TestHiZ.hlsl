#include "Culling.hlsli"
[numthreads(64,1,1)]
void CSMain(uint3 thread : SV_DispatchThreadID)
{
    uint id=thread.x;
    if(id>=info.x)return;
    Bounds b=bounds[id];
    uint visible=1;
    if(b.maximum.w==0)visible=0;
    else if(b.minimum.w==0) {
        float2 lo=float2(dimensions.xy),hi=0;float nearest=1;bool safe=true;
        for(uint c=0;c<8;++c) {
            float3 v=float3(c&1?b.maximum.x:b.minimum.x,c&2?b.maximum.y:b.minimum.y,c&4?b.maximum.z:b.minimum.z);
            float4 p=mul(float4(v,1),cullViewProjection);
            if(p.w<=1e-5 || p.z<=1e-5 || !all(isfinite(p)))safe=false;
            else {
                float2 pixel=(p.xy/p.w*float2(.5,-.5)+.5)*dimensions.xy;
                lo=min(lo,pixel);hi=max(hi,pixel);nearest=min(nearest,p.z/p.w);
            }
        }
        if(safe) {
            lo=clamp(floor(lo)-1,0,float2(dimensions.xy));hi=clamp(ceil(hi)+1,0,float2(dimensions.xy));
            if(all(hi>lo)) {
                uint2 first=(uint2)lo,last=(uint2)hi-1,size=dimensions.zw;uint offset=0;
                // Select a level where the entire rectangle touches at most 2x2 cells.
                while(any(last-first>1)) {offset+=size.x*size.y;size=max(size/2,1);first/=2;last/=2;}
                float farthest=0;
                for(uint y=first.y;y<=last.y;++y)for(uint x=first.x;x<=last.x;++x)
                    farthest=max(farthest,pyramid[offset+y*size.x+x]);
                visible=nearest>farthest+1e-5 ? 0:1;
            }
        }
    }
    predicates.Store2(id*8,uint2(visible,0));
}
