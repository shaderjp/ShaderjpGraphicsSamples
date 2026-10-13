#include "MeshScene.hlsli"
void PSMain(MeshOutput i)
{
    MeshMaterial m = meshMaterials[i.materialId];
    if (m.alpha.y != 0)
        clip(meshTextures[NonUniformResourceIndex(m.images.x)].Sample(materialSampler, i.uv).a * m.base.a - m.alpha.x);
}
