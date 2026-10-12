#pragma once
#include "../../Shared/PostProcessPanel.h"
#include "Application.h"
#include "DdsImage.h"
#include "Gltf.h"
#include "Image.h"
#include "Lighting.h"
#include <array>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

class Sample final : public tutorial::ISample
{
  public:
    void OnInit(HWND window, uint32_t width, uint32_t height, const tutorial::Options &options) override;
    void OnUpdate(float seconds) override;
    void OnRender() override;
    void OnResize(uint32_t width, uint32_t height) override;
    void OnScreenshot(const std::filesystem::path &path) override;
    void OnDestroy() noexcept override;
    bool OnMessage(HWND, UINT, WPARAM, LPARAM, LRESULT &) override;

  private:
    struct Buffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void *mapped = nullptr;
        bool coherent = false;
    };
    struct ImageResource
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    Buffer CreateBuffer(VkDeviceSize bytes, VkBufferUsageFlags usage, const void *data = nullptr);
    void DestroyBuffer(Buffer &buffer);
    void Flush(const Buffer &buffer);
    ImageResource CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                              VkImageAspectFlags aspect);
    void DestroyImage(ImageResource &image);
    uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags required);
    void CreateSwapchain();
    void DestroySwapchain();
    void CreatePipeline();
    void CreateScene(const tutorial::Options &options);
    void CreateDepth();
    void LoadTexture(const std::filesystem::path &path, uint32_t slot, VkFormat format);
    void CreateImGui();
    void CreateUiPipeline();
    void RenderImGui();
    void DestroyImGui() noexcept;
    bool win32Ui_ = false;
    tutorial::PostProcessPanel panel_;
    tutorial::LightingScene scene_, secondaryScene_;
    tutorial::GltfModel model_;
    uint32_t shadingMode_ = 0;
    float lastSeconds_ = 0;
    HWND window_ = nullptr;
    uint32_t width_ = 0, height_ = 0, queueFamily_ = 0;

    void CreatePostResources();
    void CreatePostTargets();
    void UpdatePostDescriptors();
    void CreatePostPipelines();
    void DestroyPostPipelines() noexcept;
    void DestroyPostResources() noexcept;
    void PostTransition(uint32_t, VkImageLayout);
    void DrawHdrScene(uint32_t, uint32_t);
    void DumpGpuData(const std::filesystem::path &);
    std::array<ImageResource, 5> postImages_;
    std::array<VkImageLayout, 5> postLayouts_{};
    ImageResource monitorDepth_;
    Buffer postConstants_, particles_;
    VkFormat hdrFormat_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    std::vector<VkShaderModule> postModules_;
    VkSampler postSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout postDescriptorLayout_ = VK_NULL_HANDLE, particleDescriptorLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout postLayout_ = VK_NULL_HANDLE, particleLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool postPool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 5> postSets_{};
    std::array<VkPipeline, 3> bloomPipelines_{};
    VkPipeline postPipeline_ = VK_NULL_HANDLE, testPipeline_ = VK_NULL_HANDLE,
               monitorPipeline_ = VK_NULL_HANDLE, particleCompute_ = VK_NULL_HANDLE,
               particlePipeline_ = VK_NULL_HANDLE;
    bool monitorDepthReady_ = false, particlesReady_ = false, captureOnly_ = false;
    float delta_ = 1.f / 60;
    uint32_t simulationSteps_ = 0;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<bool> presented_;
    std::vector<VkSemaphore> renderFinished_;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commands_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    bool canCapture_ = false;
    Buffer capture_;
    std::filesystem::path capturePath_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkShaderModule vertexShader_ = VK_NULL_HANDLE, pixelShader_ = VK_NULL_HANDLE;
    Buffer vertices_;
    uint32_t drawCount_ = 0;
    Buffer indices_, constants_;
    ImageResource depth_;
    bool depthReady_ = false;
    std::vector<ImageResource> textures_;
    struct LabObject
    {
        Buffer vertices, indices;
        uint32_t count = 0;
    };
    std::vector<LabObject> objects_;
    std::array<std::array<ImageResource, 3>, 3> environments_;
    ImageResource brdf_, shadow_;
    bool shadowReady_ = false;
    VkPipeline skyPipeline_ = VK_NULL_HANDLE, shadowPipeline_ = VK_NULL_HANDLE;
    VkSampler environmentSampler_ = VK_NULL_HANDLE;
    VkDeviceSize constantStride_ = 0;
    std::vector<VkDescriptorSet> descriptorSets_;
    ImageResource LoadDdsTexture(const std::filesystem::path &path);
    void UpdateLabDescriptors();
    Buffer staging_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    Buffer uiVertices_, uiIndices_, uiStaging_, uiConstants_;
    ImageResource uiFont_;
    VkSampler uiSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout uiDescriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool uiDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet uiDescriptorSet_ = VK_NULL_HANDLE;
    VkPipelineLayout uiLayout_ = VK_NULL_HANDLE;
    VkPipeline uiPipeline_ = VK_NULL_HANDLE;
    VkShaderModule uiVertexShader_ = VK_NULL_HANDLE, uiPixelShader_ = VK_NULL_HANDLE;
    size_t uiVertexCapacity_ = 0, uiIndexCapacity_ = 0;
};
