#include "MeshScene.hlsli"
MeshOutput VSMain(uint vertex : SV_VertexID)
{
    return TransformMeshVertex(dispatchInstance, vertex, 0);
}
