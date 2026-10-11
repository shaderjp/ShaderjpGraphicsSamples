#ifdef __spirv__
#define VK_BINDING(slot, descriptorSet) [[vk::binding(slot, descriptorSet)]]
#define VK_LOCATION(index) [[vk::location(index)]]
#else
#define VK_BINDING(slot, descriptorSet)
#define VK_LOCATION(index)
#endif

struct ColorInput {
    VK_LOCATION(0) float3 position : POSITION;
    VK_LOCATION(1) float4 color : COLOR;
};
struct ColorOutput {
    float4 position : SV_Position;
    VK_LOCATION(0) float4 color : COLOR;
};
struct TextureInput {
    VK_LOCATION(0) float3 position : POSITION;
    VK_LOCATION(1) float2 uv : TEXCOORD;
};
struct TextureOutput {
    float4 position : SV_Position;
    VK_LOCATION(0) float2 uv : TEXCOORD;
};
