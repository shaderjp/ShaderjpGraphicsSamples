#include "Depth.hlsli"
float Visibility(float3 world)
{
    float4 v=mul(float4(world,1),c.lightView);
    float4 clip=mul(v,c.lightProjection);
    float3 ndc=clip.xyz/clip.w;
    float2 uv=ndc.xy*float2(.5,-.5)+.5;
    if(clip.w<=0 || any(uv<0)||any(uv>=1)||ndc.z<0||ndc.z>1)return 1;
    float z=max(v.z-c.range.w,c.range.x);
    float reference;
    if(c.mode.z>0.5)reference=(z-c.range.x)/(c.range.y-c.range.x);
    else {float4 biased=mul(float4(v.xy,z,1),c.lightProjection);reference=biased.z/biased.w;}
    int2 centre=int2(uv*c.extent.z);
    int radius=c.mode.w>.5?1:0;
    float result=0;
    for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x)
    {
        int2 p=centre+int2(x,y);
        if(any(p<0)||any(p>=int(c.extent.z))){result+=1;continue;}
        float stored=c.mode.z>.5?shadowLinear.Load(int3(p,0)):shadowDepth.Load(int3(p,0));
        result+=(c.mode.z>.5&&stored<0)||reference<=stored?1:0;
    }
    return result/float((radius*2+1)*(radius*2+1));
}
struct Targets {float4 color:SV_Target0;float4 data:SV_Target1;};
Targets PSMain(Output input)
{
    Targets o;
    float visibility=c.mode.x>.5?Visibility(input.world):1;
    o.color=float4(input.color*(.18+.82*visibility),1);
    o.data=float4(input.viewZ,input.id,visibility,1);
    return o;
}
