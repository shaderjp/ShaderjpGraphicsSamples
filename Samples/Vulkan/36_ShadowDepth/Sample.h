#pragma once
#include "../../Shared/DepthPanel.h"
#include "Application.h"
#include "Image.h"
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
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
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
    void CreateImGui();
    void CreateUiPipeline();
    void RenderImGui();
    void DestroyImGui() noexcept;
    bool win32Ui_ = false;
    tutorial::depth::Panel panel_;

    void CreateResources();
    void RenderLessons();
    void CaptureData(const std::filesystem::path &path = {});
    void DestroyLessons();
    uint32_t sceneWidth_ = 1, sceneHeight_ = 1, shadowSize_ = 1;
    int activeFormat_ = -1, activeResolution_ = 0;
    std::array<std::vector<tutorial::depth::Pixel>, 2> pixels_;
    std::vector<tutorial::depth::Pixel> lightPixels_;
    std::array<tutorial::depth::Constants, 16> data_{};

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
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    Buffer vertices_;
    uint32_t drawCount_ = 0;
    Buffer constants_;

    std::array<ImageResource, 8> textures_;
    std::array<ImageResource, 3> exports_;
    std::array<VkPipeline, 2> scenePipelines_{};
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE, displayPipeline_ = VK_NULL_HANDLE,
               exportPipeline_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 16> sets_{};
    size_t constantStride_ = 512;
    void Bind(uint32_t slot);
    void UpdateDescriptors();
    void TransitionImage(ImageResource &image, VkImageLayout layout);

    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
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
