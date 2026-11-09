#include "Culling.hlsli"
[numthreads(8,8,1)]
void CSMain(uint3 thread : SV_DispatchThreadID)
{
    uint2 p=thread.xy;
    uint2 size=info.y==0 ? dimensions.zw : max(level.zw/2,1);
    if(any(p>=size))return;
    float z=1;
    if(info.y==0) {
        if(all(p<dimensions.xy))z=sourceDepth.Load(int3(p,0));
    } else {
        z=0;
        for(uint y=0;y<2;++y)for(uint x=0;x<2;++x) {
            uint2 child=min(p*2+uint2(x,y),level.zw-1);
            z=max(z,pyramid[level.x+child.y*level.z+child.x]);
        }
    }
    pyramid[level.y+p.y*size.x+p.x]=z;
}
