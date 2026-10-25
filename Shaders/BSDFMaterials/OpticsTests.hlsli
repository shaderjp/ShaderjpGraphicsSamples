// Explicit diagnostic dispatch of the production BSDF functions, read back as float32.
// It is not an alternative image renderer and is never used by normal presets.
void OpticsTest(uint2 pixel,inout uint rng) {
    HitSurface s=(HitSurface)0;
    s.normal=s.geometric=s.outward=float3(0,0,1);s.base=1;s.ior=1.5;s.transmission=1;
    uint test=pixel.x%16;
    float3 value=0;float4 sampleData=0,info=0;
    if(test==0) value=DielectricFresnel(1,1.5).xxx;
    else if(test==1) {value=refract(-float3(.5,0,sqrt(.75)),float3(0,0,1),1/1.5);sampleData=float4(value,0);}
    else if(test==2) {
        float theta=50*PI/180;
        value=float3(DielectricFresnel(cos(theta),1/1.5),length(refract(-float3(sin(theta),0,cos(theta)),float3(0,0,1),1.5)),0);
    } else if(test==10 || test==11) {
        value=DielectricFresnel((pixel.y+.5)/screen.y,test==10?1.5:1/1.5).xxx;
    } else if(test==12 || test==13) {
        uint count,stride;materials.GetDimensions(count,stride);
        for(uint i=0;i<count;++i)if(any(materials[i].absorption.rgb>0)) {
            value=SegmentTransmission(i+1,test==12?.5:2);info=float4(materials[i].absorption.rgb,0);break;
        }
    } else if(test==14) value=float3(PowerWeight(1,2),PowerWeight(2,1),1);
    else {
        float3 V=float3(0,0,1);
        if(test==4 || test==8 || test==9)s.outward=-s.outward;
        if(test==5){s.ior=1;s.roughness=.3;}
        if(test>=6 && test<=9)s.roughness=.3;
        if(test==7)V=normalize(float3(.95,0,.312));
        if(test==9)V=normalize(float3(.8,0,.6));
        if(test==15){s.transmission=0;s.roughness=.3;s.base=float3(.8,.4,.2);}
        ScatterSample a=SampleSurface(s,V,rng);
        value=a.weight;sampleData=float4(a.direction,a.pdf);info=float4(a.weight,float(a.flags));
    }
    outputImage[pixel]=float4(value,1);
    reflectionImage[pixel]=sampleData;surfaceImage[pixel]=info;
}
