#pragma once
#include "../../Shared/BasicMeshData.h"
#include "Application.h"
#include "Gltf.h"
#include "Image.h"
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
    void LoadTexture(const std::filesystem::path &path);
    tutorial::GltfModel model_;
    tutorial::BasicMeshPanel panel_;
    tutorial::Options options_;
    void UpdateConstants();
    bool OnMessage(HWND, UINT, WPARAM, LPARAM, LRESULT &) override;
    HWND window_ = nullptr;
    uint32_t width_ = 0, height_ = 0, queueFamily_ = 0;
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
    VkPipeline pipeline_ = VK_NULL_HANDLE, meshPipeline_ = VK_NULL_HANDLE;
    VkShaderModule meshShader_ = VK_NULL_HANDLE;
    PFN_vkCmdDrawMeshTasksEXT drawMesh_ = nullptr;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkShaderModule vertexShader_ = VK_NULL_HANDLE, pixelShader_ = VK_NULL_HANDLE;
    Buffer vertices_;
    uint32_t drawCount_ = 0;
    Buffer indices_, constants_;
    ImageResource depth_;
    bool depthReady_ = false;
    ImageResource texture_;
    Buffer staging_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    void CreateImGui();
    void CreateUiPipeline();
    void RenderImGui();
    void DestroyImGui() noexcept;
    bool win32Ui_ = false;
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
