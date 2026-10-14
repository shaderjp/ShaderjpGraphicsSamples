#include "MeshScene.hlsli"
struct CacheInput
{
    VK_LOCATION(0) float4 positionU : POSITION;
    VK_LOCATION(1) float4 normalV : NORMAL;
    VK_LOCATION(2) float4 tangent : TANGENT;
};
MeshOutput VSMain(CacheInput i, uint index : SV_VertexID)
{
    MeshVertex v; v.positionU = i.positionU; v.normalV = i.normalV; v.tangent = i.tangent;
    MeshInstance object = meshInstances[dispatchInstance];
    uint sourceIndex = meshPrimitives[object.primitive.x].offsets.x + index;
    return TransformCachedVertex(dispatchInstance, v, 0, sourceIndex);
}
