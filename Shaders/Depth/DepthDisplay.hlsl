#include "Depth.hlsli"
float4 PSMain(FullOutput input):SV_Target0
{
    bool right=c.display.x>.5;
    int kind=int(c.display.y);
    int2 p=min(int2(input.uv*c.extent.xy),int2(c.extent.xy)-1);
    float d=right?depth1.Load(int3(p,0)):depth0.Load(int3(p,0));
    float4 data=right?data1.Load(int3(p,0)):data0.Load(int3(p,0));
    if(kind==3)return float4(d,data.x,data.y,data.w);
    if(kind==4)
    {
        int2 sp=min(int2(input.uv*c.extent.z),int(c.extent.z)-1);
        float lin=shadowLinear.Load(int3(sp,0));
        return float4(shadowDepth.Load(int3(sp,0)),lin,0,lin>=0?1:0);
    }
    if(kind==0)return right?color1.Load(int3(p,0)):color0.Load(int3(p,0));
    if(c.mode.x>.5)
    {
        int2 sp=min(int2(input.uv*c.extent.z),int(c.extent.z)-1);
        float ld=shadowDepth.Load(int3(sp,0)),linearValue=shadowLinear.Load(int3(sp,0));
        if(kind==1){float v=right?max(linearValue,0):ld;return float4(v.xxx,1);}
        if(right)return data.w>0?float4(lerp(float3(.07,.1,.18),float3(.2,.8,.65),data.z),1):float4(.015,.025,.04,1);
        float z=Decode(ld,false,c.lightProjection[3][3]>.5,false,c.range.x,c.range.y);
        float value=saturate(z/c.range.z);
        return linearValue>=0?float4(value.xxx,1):float4(.015,.025,.04,1);
    }
    if(kind==1)return float4(d.xxx,1);
    if(data.w==0)return float4(.015,.025,.04,1);
    float z=Decode(d,right,c.display.z>.5,c.display.w>.5,c.range.x,c.range.y);
    float v=saturate(z/c.range.z);
    return float4(v.xxx,1);
}
