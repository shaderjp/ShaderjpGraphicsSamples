#pragma once
#include "../../Shared/PostProcessData.h"
#include "../../Shared/PathTracingPanel.h"
#include "DdsImage.h"
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
class Sample final : public tutorial::ISample
{
  public:
    void OnInit(HWND, uint32_t, uint32_t, const tutorial::Options &) override;
    void OnUpdate(float) override;
    void OnRender() override;
    void OnResize(uint32_t, uint32_t) override;
    void OnScreenshot(const std::filesystem::path &) override;
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
    tutorial::RaySceneData rayData_;
    std::vector<tutorial::RayInstance> rayInstances_;
    Buffer rayVertices_, rayIndices_, rayPrimitives_, rayMaterials_, rayInstancesBuffer_, shaderTable_;
    struct Acceleration
    {
        Buffer storage;
        VkAccelerationStructureKHR object = VK_NULL_HANDLE;
    };
    std::vector<Acceleration> blas_;
    Acceleration tlas_;
    std::array<ImageResource, 2> diagnostic_;
    std::array<VkImageLayout, 2> diagnosticLayouts_{};
    VkPipeline rayPipeline_ = VK_NULL_HANDLE;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties_{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties_{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkStridedDeviceAddressRegionKHR raygenRegion_{}, missRegion_{}, hitRegion_{}, callableRegion_{};
    PFN_vkCreateAccelerationStructureKHR createAS_ = nullptr;
    PFN_vkDestroyAccelerationStructureKHR destroyAS_ = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR getBuildSizes_ = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR buildAS_ = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR getASAddress_ = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR createRTP_ = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR getShaderHandles_ = nullptr;
    PFN_vkCmdTraceRaysKHR traceRays_ = nullptr;
    int builtScene_ = -1, builtPreset_ = -1;
    float builtOccluder_ = 100;
    VkDeviceAddress Address(const Buffer &);
    Buffer DeviceBuffer(VkDeviceSize, VkBufferUsageFlags);
    void BuildBlas();
    void BuildTlas();
    void CreateRayPipeline();
    void DestroyAcceleration(Acceleration &);
    HWND window_ = nullptr;
    uint32_t width_ = 0, height_ = 0, queueFamily_ = 0;
    tutorial::Workshop workshop_;
    tutorial::PathTracingPanel panel_;
    tutorial::PathLightData pathLights_;
    Buffer areaLights_;
    uint32_t samples_ = 0;
    std::array<uint8_t, sizeof(tutorial::RayConstants)> previousConstants_{};
    bool havePrevious_ = false;
    std::string gpuName_;
    tutorial::Options options_;
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
    bool canCapture_ = false, captureOnly_ = false;
    Buffer capture_, staging_;
    std::filesystem::path capturePath_;
    std::array<ImageResource, 4> hdr_;
    std::array<VkImageLayout, 4> hdrLayouts_{};
    ImageResource brdf_;
    std::array<ImageResource, 3> environment_;
    std::vector<ImageResource> textures_;
    Buffer constants_, points_, postConstants_;
    VkDeviceSize constantStride_ = 0;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE, postDescriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE, postPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;
    std::array<VkDescriptorSet, 4> postSets_{};
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE, postLayout_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE, environmentSampler_ = VK_NULL_HANDLE;
    VkPipeline postPipeline_ = VK_NULL_HANDLE;
    std::vector<VkShaderModule> modules_;
    uint32_t MemoryType(uint32_t, VkMemoryPropertyFlags);
    Buffer CreateBuffer(VkDeviceSize, VkBufferUsageFlags, const void *data = nullptr);
    void DestroyBuffer(Buffer &);
    void Flush(const Buffer &);
    ImageResource CreateImage(uint32_t, uint32_t, VkFormat, VkImageUsageFlags, VkImageAspectFlags, uint32_t mips = 1);
    void DestroyImage(ImageResource &);
    ImageResource LoadDdsTexture(const std::filesystem::path &);
    void CreateSwapchain();
    void DestroySwapchain();
    void CreateScene();
    void CreateSizeResources();
    void UpdateDescriptors();
    void CreatePipeline();
    void DestroyPipelines();
    void Move(VkImage, VkImageAspectFlags, VkImageLayout &, VkImageLayout);
    void Begin(const std::vector<VkImageView> &colors, VkImageView depth, bool clear, bool readOnly, uint32_t width,
               uint32_t height);
    void Submit();
    void DumpGpuData(const std::filesystem::path &);
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
