#pragma once
#include "../../Shared/BindlessPanel.h"
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
    struct Acceleration
    {
        Buffer storage;
        VkAccelerationStructureKHR object = VK_NULL_HANDLE;
    };
    HWND window_ = nullptr;
    uint32_t width_ = 0, height_ = 0, queueFamily_ = 0, timestampBits_ = 0;
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
    tutorial::Options options_, initialOptions_;
    tutorial::GalleryData data_;
    tutorial::GalleryPanel panel_;
    tutorial::GalleryTimings timing_;
    tutorial::GalleryConstants frame_{};
    std::vector<uint32_t> boundSetForInstance_, boundGeometry_, boundMaterial_;
    std::vector<Buffer> buffers_;
    std::vector<ImageResource> textures_;
    std::array<ImageResource, 5> hdr_{};
    std::array<VkImageLayout, 5> hdrLayouts_{};
    ImageResource depth_;
    VkImageLayout depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    Buffer constants_, visibility_, shaderTable_;
    VkQueryPool timestamps_ = VK_NULL_HANDLE;
    double timestampPeriod_ = 0;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkPipeline raster_ = VK_NULL_HANDLE, mesh_ = VK_NULL_HANDLE, display_ = VK_NULL_HANDLE, light_ = VK_NULL_HANDLE,
               reset_ = VK_NULL_HANDLE, cull_ = VK_NULL_HANDLE, finalize_ = VK_NULL_HANDLE,
               rayPipeline_ = VK_NULL_HANDLE;
    VkPipeline countReset_ = VK_NULL_HANDLE, countVisible_ = VK_NULL_HANDLE;
    std::vector<VkShaderModule> modules_;
    VkShaderStageFlags shaderStages_ = 0;
    bool canCapture_ = false, captureOnly_ = false;
    Buffer capture_;
    std::filesystem::path capturePath_;
    std::vector<Acceleration> blas_;
    Acceleration tlas_;
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
    PFN_vkCmdDrawMeshTasksEXT drawMesh_ = nullptr;
    PFN_vkCmdDrawMeshTasksIndirectEXT drawMeshIndirect_ = nullptr;
    uint32_t MemoryType(uint32_t, VkMemoryPropertyFlags);
    Buffer CreateBuffer(VkDeviceSize, VkBufferUsageFlags, const void *data = nullptr);
    Buffer DeviceBuffer(VkDeviceSize, VkBufferUsageFlags);
    void DestroyBuffer(Buffer &);
    void Flush(const Buffer &);
    void DestroyImage(ImageResource &);
    ImageResource CreateImage(uint32_t, uint32_t, VkFormat, VkImageUsageFlags, VkImageAspectFlags, uint32_t mips = 1,
                              uint32_t layers = 1, bool array = false);
    ImageResource LoadTexture(const std::vector<uint32_t> &, uint32_t);
    void CreateSwapchain();
    void DestroySwapchain();
    void CreateScene();
    void CreateSizeResources();
    void UpdateDescriptors();
    void CreatePipeline();
    void DestroyPipelines();
    void DestroyScene();
    void BeginCommands();
    void Submit();
    void Move(VkImage, VkImageAspectFlags, VkImageLayout &, VkImageLayout);
    void Begin(const std::vector<VkImageView> &, VkImageView, bool, bool, uint32_t, uint32_t);
    void Barrier();
    void Bind(VkPipelineBindPoint, uint32_t = 0, bool = false);
    void Stamp(uint32_t);
    VkDeviceAddress Address(const Buffer &);
    void BuildAcceleration();
    void DestroyAcceleration(Acceleration &);
    void CreateRayPipeline();
    void DumpGpuData(const std::filesystem::path &);
    void CollectStatistics();
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
