#include "Lighting.hlsli"
float4 PSMain(SurfaceVertex i,bool front:SV_IsFrontFace):SV_Target{Surface s=ReadMaterial(i,front);return float4(Shade(s,i.worldPosition),s.alpha);}
