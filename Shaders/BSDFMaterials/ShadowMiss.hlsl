#include "Scene.hlsli"
[shader("miss")] void ShadowMiss(inout Payload p) { p.distance = -1; }
