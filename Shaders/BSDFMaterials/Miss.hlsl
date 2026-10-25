#include "Scene.hlsli"
[shader("miss")] void Miss(inout Payload p) { p.distance = -1; }
