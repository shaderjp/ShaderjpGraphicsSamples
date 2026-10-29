#include "Scene.hlsli"
[shader("miss")] void ShadowMiss(inout Payload payload) { payload.color = 1; }
