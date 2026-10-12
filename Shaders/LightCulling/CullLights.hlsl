#include "../Deferred/Resources.hlsli"
#define BUILD_LIGHT_LISTS
#include "Grid.hlsli"
groupshared uint candidateCount;
bool IntersectsCell(float3 c,float r,float4 slopes,float2 z)
{
    float epsilon=max(.001,z.y*.0001);
    if(c.z+r<z.x-epsilon || c.z-r>z.y+epsilon)return false;
    float4 distances=float4(c.x-slopes.x*c.z,slopes.y*c.z-c.x,
                            c.y-slopes.z*c.z,slopes.w*c.z-c.y);
    return all(distances>=-(r+epsilon)*sqrt(1+slopes*slopes));
}
[numthreads(64,1,1)]
void CSMain(uint3 group:SV_GroupID,uint thread:SV_GroupIndex)
{
    uint tile=group.y*gridSize.x+group.x;
    uint cell=(group.z*gridSize.y+group.y)*gridSize.x+group.x;
    uint mask=tileSliceMasks[tile];
    bool active=gridControls.x==2 ? (mask&(1u<<group.z))!=0 : mask!=0;
    if(!active){if(thread==0)lightHeaders[cell]=uint2(0,0);return;}
    float2 z=tileDepthBounds[tile];
    if(gridControls.x==2)
    {
        float2 slice=depthRange.x*pow(depthRange.y/depthRange.x,float2(group.z,group.z+1)/gridSize.z);
        // Only opaque samples marked this cell. Trim the slice's empty end regions.
        z=float2(max(z.x,slice.x),min(z.y,slice.y));
    }
    float2 lo=float2(group.xy*16)/screen.xy;
    float2 hi=float2(min((group.xy+1)*16,uint2(screen.xy)))/screen.xy;
    float4 slopes=float4(lo.x*2-1,hi.x*2-1,1-hi.y*2,1-lo.y*2)
                    *projectionInfo.xxyy;
    if(thread==0)candidateCount=0;
    GroupMemoryBarrierWithGroupSync();
    for(uint i=thread;i<controls.x;i+=64)
    {
        PointLight l=points[i];
        float3 center=mul(float4(l.positionRange.xyz,1),worldToView).xyz;
        // range==0 is an unbounded light: never discard it spatially.
        if(l.positionRange.w<=0 || IntersectsCell(center,l.positionRange.w,slopes,z))
        {
            uint slot;InterlockedAdd(candidateCount,1,slot);
            if(slot<gridControls.z)lightIndices[cell*gridSize.w+slot]=i;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if(thread==0)lightHeaders[cell]=uint2(candidateCount,candidateCount>gridControls.z);
}
