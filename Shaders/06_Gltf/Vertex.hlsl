#include "Scene.hlsli"
TextureOutput VSMain(TextureInput input)
{
    TextureOutput output;
    output.position = mul(float4(input.position, 1.0), mvp);
    output.uv = input.uv;
    return output;
}
