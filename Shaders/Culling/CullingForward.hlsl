#include "../Deferred/Lighting.hlsli"
float4 PSMain(SurfaceVertex i, bool front : SV_IsFrontFace) : SV_Target
{
    Surface s = ReadMaterial(i, front);
    if (shadow.z == 4) {
        uint id = uint(shadow.w);
        return float4(float(id & 255) / 255, float((id >> 8) & 255) / 255, 0, 1);
    }
    return float4(Shade(s, i.worldPosition), s.alpha);
}
