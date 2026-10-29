struct Instance { row_major float4x4 world, normalMatrix; uint4 primitive; };
cbuffer Simulation : register(b0)
{
    float time; uint count, side, activePercent;
    uint globalPartition, initializeAll, partitionSide, unused;
};
StructuredBuffer<Instance> originalInstances : register(t0);
ByteAddressBuffer originalRegular : register(t1);
ByteAddressBuffer originalPartitioned : register(t2);
RWStructuredBuffer<Instance> currentInstances : register(u0);
RWByteAddressBuffer regularInstances : register(u1);
RWByteAddressBuffer partitionedWrites : register(u2);
RWByteAddressBuffer operation : register(u3);
RWStructuredBuffer<uint2> previous : register(u4);

[numthreads(64,1,1)]
void CSMain(uint i:SV_DispatchThreadID)
{
    if(i>=count)return;
    Instance inst=originalInstances[i];
    float angle=0;bool active=false;
    uint partition=i==0?0:((i-1)%side)/4+((i-1)/side)/4*partitionSide;
    if(i>0 && activePercent>0)
    {
        float phase=frac(time*.1-float(i-1)/float(count-1));
        float width=activePercent*.01;
        active=phase<width;
        if(active)angle=.65*sin(phase/width*6.28318530718);
    }
    if(globalPartition!=0 && active && i>0)partition=0xffffffff;
    float s,c;sincos(angle,s,c);
    float3 translation=inst.world[3].xyz;
    inst.world=float4x4(c,s,0,0,-s,c,0,0,0,0,1,0,translation.x,translation.y,translation.z,1);
    inst.normalMatrix=float4x4(c,s,0,0,-s,c,0,0,0,0,1,0,0,0,0,1);
    inst.primitive.z=partition;inst.primitive.w=active?1:0;currentInstances[i]=inst;
    for(uint j=0;j<64;j+=4)regularInstances.Store(i*64+j,originalRegular.Load(i*64+j));
    for(uint row=0;row<3;++row)
        regularInstances.Store4(i*64+row*16,asuint(float4(inst.world[0][row],inst.world[1][row],inst.world[2][row],translation[row])));
    uint2 state=uint2(asuint(angle),partition);
    bool changed=initializeAll!=0||any(previous[i]!=state);
    previous[i]=state;
    if(changed)
    {
        uint slot;operation.InterlockedAdd(4,1,slot);
        // Every thread emits at most one write; capacity is the full instance count.
        for(uint k=0;k<104;k+=4)partitionedWrites.Store(slot*104+k,originalPartitioned.Load(i*104+k));
        for(uint row=0;row<3;++row)
            partitionedWrites.Store4(slot*104+row*16,asuint(float4(inst.world[0][row],inst.world[1][row],inst.world[2][row],translation[row])));
        partitionedWrites.Store(slot*104+76,partition);
    }
}
