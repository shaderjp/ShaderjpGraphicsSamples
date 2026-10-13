#include "Scene.hlsli"
Output VSMain(uint index : SV_VertexID)
{
    return Transform(index);
}
