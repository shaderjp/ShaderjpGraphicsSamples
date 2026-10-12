#include "Post.hlsli"
struct Particle
{
    float4 positionSize, velocityAge, color;
};
VK_BINDING(4, 0) RWStructuredBuffer<Particle> particles : register(u0);
uint Hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    return x ^ (x >> 16);
}
float Random(uint x)
{
    return (Hash(x) & 0xffffff) / 16777216.0;
}
Particle Spawn(uint id, float age)
{
    Particle p;
    float angle = Random(id * 3 + 1) * 6.2831853, spread = .25 + Random(id * 3 + 2) * .5;
    float3 v = float3(cos(angle) * spread, 2.8 + Random(id * 3 + 3) * 1.5, sin(angle) * spread);
    p.positionSize = float4(emitter.xyz + v * age - float3(0, .5 * simulation.y * age * age, 0), emitter.w);
    p.velocityAge = float4(v - float3(0, simulation.y * age, 0), age);
    p.color = float4(8, 2.4, .35, 1);
    return p;
}
[numthreads(256, 1, 1)] void CSMain(uint3 id : SV_DispatchThreadID) {
    uint count = simulation.w != 0 ? 65536 : uint(simulation.z);
    if (id.x >= count)
        return;
    Particle p;
    if (simulation.w != 0)
        p = Spawn(id.x, Random(id.x + 773) * .9);
    else
    {
        p = particles[id.x];
        p.positionSize.xyz += p.velocityAge.xyz * simulation.x;
        p.velocityAge.y -= simulation.y * simulation.x;
        p.velocityAge.w += simulation.x;
        if (p.velocityAge.w > 1.5 || p.positionSize.y < .04)
            p = Spawn(id.x, 0);
    }
    particles[id.x] = p;
}
