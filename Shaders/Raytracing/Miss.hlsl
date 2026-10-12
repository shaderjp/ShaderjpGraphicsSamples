#include "Scene.hlsli"
[shader("miss")] void Miss(inout Payload payload) {
#if RT_TECHNIQUE == 1
    payload.color = float3(.02, .025, .035);
#else
    payload.color = lighting.y != 0
                        ? environmentTexture.SampleLevel(environmentSampler, EnvDirection(WorldRayDirection()), 0).rgb *
                              environment.x
                        : 0;
    if (payload.bounce == 0 && controls.y != 0)
        payload.color = 0;
#endif
}
