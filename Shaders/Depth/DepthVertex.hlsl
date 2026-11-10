#include "Depth.hlsli"
Output VSMain(Input input)
{
    Output o;
    float4 v=mul(float4(input.position,1),c.view);
    // Projection is a separate operation. Reverse-Z is generated here, before quantization.
    o.position=mul(v,c.projection);
    o.color=input.color;o.world=input.position;o.viewZ=v.z;o.id=input.id;
    return o;
}
