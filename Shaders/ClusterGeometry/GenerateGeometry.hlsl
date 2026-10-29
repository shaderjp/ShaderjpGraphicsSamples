// Native DXR argument layouts are checked with static_assert in Sample.cpp.
struct Vertex { float4 positionU, normalV, tangent; };
struct Cluster { uint4 offsets, extra; };
cbuffer Frame : register(b0)
{
    float time; uint vertexCount, clusterCount, templateEnabled;
    uint2 vertexAddress, indexAddress;
    uint tileFirst, tileSide, fixedRate, screenHeight;
    float3 eye; float fov;
};
StructuredBuffer<Vertex> rest : register(t0);
StructuredBuffer<Cluster> source : register(t1);
ByteAddressBuffer templateAddresses : register(t2);
RWStructuredBuffer<Vertex> outputVertices : register(u0);
RWByteAddressBuffer directArguments : register(u1);
RWStructuredBuffer<Cluster> outputClusters : register(u2);
RWByteAddressBuffer instantiateArguments : register(u3);

uint2 Address(uint2 base, uint offset)
{
    uint low=base.x+offset; return uint2(low,base.y+uint(low<base.x));
}
uint Desired(uint i)
{
    if(fixedRate!=0) return fixedRate==2?0:fixedRate==4?1:2;
    float2 center=(float2(i%tileSide,i/tileSide)-float(tileSide)*.5)*2+1;
    float distance=max(length(eye-float3(center.x,1,center.y)),1);
    float pixels=screenHeight/tan(fov*.00872664626)/distance;
    return pixels>320?2:pixels>160?1:0;
}
uint Level(uint i)
{
    uint value=Desired(i),x=i%tileSide,y=i/tileSide;
    if(x>0)value=max(value,uint(max(int(Desired(i-1))-1,0)));
    if(x+1<tileSide)value=max(value,uint(max(int(Desired(i+1))-1,0)));
    if(y>0)value=max(value,uint(max(int(Desired(i-tileSide))-1,0)));
    if(y+1<tileSide)value=max(value,uint(max(int(Desired(i+tileSide))-1,0)));
    return value;
}
// All 33 regular topologies follow the static-cluster templates. Their descriptions
// are passed in the source metadata after the active cluster range.
Cluster Select(uint id)
{
    Cluster c=source[id];
    if(c.extra.z!=2) return c;
    uint i=c.extra.w,x=i%tileSide,y=i/tileSide,level=Level(i),mask=0;
    if(y>0 && Level(i-tileSide)>level)mask|=1;
    if(x+1<tileSide && Level(i+1)>level)mask|=2;
    if(y+1<tileSide && Level(i+tileSide)>level)mask|=4;
    if(x>0 && Level(i-1)>level)mask|=8;
    uint t=level==2?32:level*16+mask;
    Cluster topology=source[clusterCount+t];
    c.offsets.yzw=topology.offsets.yzw;c.extra.y=tileFirst+t;
    return c;
}
[numthreads(64,1,1)]
void CSMain(uint3 group:SV_GroupID,uint lane:SV_GroupIndex)
{
    uint id=group.x;if(id>=clusterCount)return;
    Cluster c=Select(id);
    uint count=c.extra.z==2?81:c.offsets.w;
    for(uint local=lane;local<count;local+=64)
    {
        uint i=c.offsets.x+local;Vertex v=rest[i];float x=v.positionU.x,z=v.positionU.z;
        if(c.extra.z==1)
        {
            float t=x*1.7+time,weight=v.normalV.w;
            float2 gradient=-v.normalV.xy/max(v.normalV.z,1e-6);
            gradient+=float2(.85*cos(t)*weight,.5*sin(t)/asfloat(c.extra.w));
            v.positionU.z+=.5*sin(t)*weight;
            v.normalV.xyz=normalize(float3(-gradient,1));
            v.tangent.xyz=normalize(float3(1,0,gradient.x));
        }
        if(c.extra.z==2)
        {
            float a=x*1.3+time*.5,b=z*1.1,crossing=x*8+z*6;
            v.positionU.y=.8+.3*sin(a)*cos(b)+.08*sin(crossing);
            float dx=.39*cos(a)*cos(b)+.64*cos(crossing);
            float dz=-.33*sin(a)*sin(b)+.48*cos(crossing);
            v.normalV.xyz=normalize(float3(-dx,1,-dz));v.tangent.xyz=normalize(float3(1,dx,0));
        }
        outputVertices[i]=v;
    }
    if(lane==0)
    {
        outputClusters[id]=c;
        uint offset=id*80;
        for(uint j=0;j<80;j+=4)directArguments.Store(offset+j,0);
        directArguments.Store(offset,id);
        directArguments.Store(offset+8,c.offsets.z|(c.offsets.w<<16));
        directArguments.Store(offset+12,0x80000000u); // OPAQUE, GeometryIndex=0
        directArguments.Store(offset+20,48u|(4u<<16));
        directArguments.Store2(offset+32,Address(vertexAddress,c.offsets.x*48));
        directArguments.Store2(offset+40,Address(indexAddress,c.offsets.y*4));
        offset=id*32;
        instantiateArguments.Store2(offset,uint2(0,id));
        uint2 templateAddress=0;
        if(templateEnabled!=0)templateAddress=templateAddresses.Load2(c.extra.y*8);
        instantiateArguments.Store2(offset+8,templateAddress);
        instantiateArguments.Store2(offset+16,Address(vertexAddress,c.offsets.x*48));
        instantiateArguments.Store2(offset+24,uint2(48,0));
    }
}
