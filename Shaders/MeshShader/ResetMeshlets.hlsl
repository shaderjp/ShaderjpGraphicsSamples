#include "MeshScene.hlsli"
[numthreads(64, 1, 1)] void CSMain(uint thread : SV_GroupIndex) { meshArguments.Store(thread * 4, 0); }
