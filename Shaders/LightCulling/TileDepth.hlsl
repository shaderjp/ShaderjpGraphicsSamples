#include "../Deferred/Resources.hlsli"
#define BUILD_LIGHT_LISTS
#include "Grid.hlsli"
VK_BINDING(17,0) Texture2D<float> gDepth:register(t14);
groupshared uint nearestZ,farthestZ,sliceMask;
[numthreads(16,16,1)]
void CSMain(uint3 tile:SV_GroupID,uint3 pixel:SV_DispatchThreadID,uint thread:SV_GroupIndex)
{
    if(thread==0){nearestZ=0x7f800000;farthestZ=0;sliceMask=0;}
    GroupMemoryBarrierWithGroupSync();
    // Threads outside an odd-sized image still participate in both barriers.
    if(all(pixel.xy<uint2(screen.xy)))
    {
        float d=gDepth.Load(int3(pixel.xy,0));
        if(d<1)
        {
            float z=ViewDepth(d);
            InterlockedMin(nearestZ,asuint(z));InterlockedMax(farthestZ,asuint(z));
            InterlockedOr(sliceMask,1u<<DepthSlice(z));
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if(thread==0)
    {
        uint t=tile.y*gridSize.x+tile.x;
        tileDepthBounds[t]=sliceMask!=0 ? float2(asfloat(nearestZ),asfloat(farthestZ)) : float2(0,0);
        tileSliceMasks[t]=sliceMask;
    }
}
