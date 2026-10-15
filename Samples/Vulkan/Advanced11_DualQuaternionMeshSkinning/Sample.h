#pragma once
#include "../../Shared/DualQuaternionSkinningTiming.h"
#include "../../Shared/PostProcessData.h"
#include "../../Shared/DualQuaternionSkinningPanel.h"
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
    HWND window_ = nullptr;
    uint32_t width_ = 0, height_ = 0, queueFamily_ = 0, timestampBits_ = 0;
    tutorial::Workshop workshop_;
    tutorial::DualQuaternionSkinningData skinning_;
    void CreateSkinResources();
    void UpdateSkinBuffers();
    void DumpSkinningData(const std::filesystem::path &);
    tutorial::DualQuaternionSkinningPanel panel_;
    tutorial::DualQuaternionSkinningTiming timing_;
    double meshCpuFrame_ = 0;
    void DumpMeshData(const std::filesystem::path &, nlohmann::json &);
    tutorial::Options options_;
    tutorial::MeshletData meshData_;
    std::vector<tutorial::MeshInstance> meshInstances_;
    std::vector<DirectX::XMUINT4> meshCandidates_;
    std::array<tutorial::MeshFrame, 2> meshFrames_{};
    uint32_t candidateCapacity_ = 1, meshDispatchWidth_ = 256;
    bool meshStatisticsReady_ = false;
    void CreateMeshResources();
    void UpdateMeshDescriptors();
    void UpdateMeshData(const tutorial::DeferredConstants &);
    void CreateMeshPipeline();
    void ResetMeshStatistics();
    void CopyMeshStatistics();

    std::vector<tutorial::WorkshopLight> frameLights_;
    tutorial::LightGridConstants gridData_{};
    uint32_t gridCells_ = 0;
    float previousSeconds_ = 0;
    Buffer gridConstants_, gridStatsReadback_;
    std::array<Buffer, 4> gridBuffers_;
    VkPipeline tileDepthPipeline_ = VK_NULL_HANDLE, cullPipeline_ = VK_NULL_HANDLE;
    void CreateLightGridResources();
    void BuildLightLists();
    void UpdateLightGridTestState();
    void CaptureLightGridTestState();
    void CopyGridStatistics();
    void ReadGridStatistics();
    void DumpLightGrid(const std::filesystem::path &);

    std::array<Buffer, 10> meshBuffers_;
    std::array<Buffer, 4> skinBuffers_;
    Buffer skinCache_;
    VkPipeline skinCompute_ = VK_NULL_HANDLE;
    Buffer meshVisible_, meshArgs_, meshReadback_;
    std::array<VkPipeline, 6> meshDirect_{}, meshTask_{};
    VkPipeline meshReset_ = VK_NULL_HANDLE, meshCull_ = VK_NULL_HANDLE, meshFinalize_ = VK_NULL_HANDLE;
    VkShaderStageFlags meshStageFlags_ = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                         VK_SHADER_STAGE_COMPUTE_BIT |
                                         (SKIN_TECHNIQUE == 6 ? VK_SHADER_STAGE_MESH_BIT_EXT : 0);
    PFN_vkCmdDrawMeshTasksEXT drawMesh_ = nullptr;
    PFN_vkCmdDrawMeshTasksIndirectEXT drawMeshIndirect_ = nullptr;
    void BindMesh(int, uint32_t, uint32_t, uint32_t, bool);
    void MeshBarrier(VkPipelineStageFlags2, VkAccessFlags2, VkPipelineStageFlags2, VkAccessFlags2);
    void PrepareMeshIndirect();
    void DrawMeshIndirect(int);
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
    std::array<ImageResource, 5> gbuffer_;
    std::array<VkImageLayout, 5> gLayouts_{};
    std::array<ImageResource, 4> hdr_;
    std::array<VkImageLayout, 4> hdrLayouts_{};
    ImageResource shadow_, brdf_;
    VkImageLayout shadowLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    std::array<ImageResource, 3> environment_;
    std::vector<ImageResource> textures_;
    struct Geometry
    {
        Buffer vertices, indices;
        uint32_t count = 0;
    };
    std::vector<Geometry> geometry_;
    Buffer constants_, points_, postConstants_;
    VkDeviceSize constantStride_ = 0;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE, postDescriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE, postPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;
    std::array<VkDescriptorSet, 4> postSets_{};
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE, postLayout_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE, environmentSampler_ = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> gPipelines_{}, forwardPipelines_{}, transparentPipelines_{}, shadowPipelines_{},
        bloomPipelines_{};
    VkPipeline lightingPixel_ = VK_NULL_HANDLE, lightingCompute_ = VK_NULL_HANDLE,
               skyPipeline_ = VK_NULL_HANDLE, postPipeline_ = VK_NULL_HANDLE, testPipeline_ = VK_NULL_HANDLE;
    std::vector<VkShaderModule> modules_;
    VkQueryPool queries_ = VK_NULL_HANDLE;
    double timestampPeriod_ = 0;
    uint32_t MemoryType(uint32_t, VkMemoryPropertyFlags);
    Buffer CreateBuffer(VkDeviceSize, VkBufferUsageFlags, const void *data = nullptr, bool local = false);
    void DestroyBuffer(Buffer &);
    void Flush(const Buffer &);
    ImageResource CreateImage(uint32_t, uint32_t, VkFormat, VkImageUsageFlags, VkImageAspectFlags,
                              uint32_t mips = 1);
    void DestroyImage(ImageResource &);
    ImageResource LoadDdsTexture(const std::filesystem::path &);
    void CreateSwapchain();
    void DestroySwapchain();
    void CreateScene();
    void CreateSizeResources();
    void UpdateDescriptors();
    void CreatePipeline();
    void DestroyPipelines();
    void Bind(uint32_t object, uint32_t material, bool compute = false);
    void DrawObjects(int pass);
    void Move(VkImage, VkImageAspectFlags, VkImageLayout &, VkImageLayout);
    void Stamp(uint32_t);
    void Begin(const std::vector<VkImageView> &colors, VkImageView depth, bool clear, bool readOnly,
               uint32_t width, uint32_t height);
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
