#include "Culling.hlsli"
[numthreads(64,1,1)]
void CSMain(uint3 thread : SV_DispatchThreadID)
{
    if(thread.x>=info.x)return;
    uint2 value=predicates.Load2(thread.x*8);
    // Vulkan conditional rendering consumes 32 bits; preserve nonzero 64-bit query results.
    predicates.Store2(thread.x*8,uint2(any(value!=0)?1:0,0));
}
