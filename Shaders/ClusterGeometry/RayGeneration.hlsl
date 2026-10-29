#include "Scene.hlsli"
[shader("raygeneration")] void RayGeneration() {
    uint2 pixel = DispatchRaysIndex().xy;
    float2 ndc = (float2(pixel) + .5) / screen.xy * float2(2, -2) + float2(-1, 1);
    float4 nearP = mul(float4(ndc, 0, 1), inverseViewProjection), farP = mul(float4(ndc, 1, 1), inverseViewProjection);
    nearP /= nearP.w;
    farP /= farP.w;
    RayDesc ray;
    ray.Origin = camera.xyz;
    ray.Direction = SafeNormalize(farP.xyz - camera.xyz);
    ray.TMin = length(nearP.xyz - camera.xyz);
    ray.TMax = length(farP.xyz - camera.xyz);
    Payload p;
    p.color = 0;
    p.bounce = 0;
    surfaceImage[pixel] = float4(0, 0, -1, -1);
    hitIdsImage[pixel] = -1;
    TraceRay(sceneAS, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 255, 0, RayStride, 0, ray, p);
    if (controls.y == 3)
    {
        float distance = surfaceImage[pixel].z;
        float4 q = mul(float4(camera.xyz + ray.Direction * max(distance, 0), 1), viewProjection);
        p.color = distance < 0 ? 1.xxx : saturate(q.z / q.w).xxx;
    }
    outputImage[pixel] = float4(max(p.color, 0), 1);
}
