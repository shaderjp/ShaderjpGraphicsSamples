#pragma once
#include "../../Shared/ClusterGeometryData.h"
#include "../../Shared/PostProcessData.h"
#include "../../Shared/RaytracingPanel.h"
#include "DdsImage.h"
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
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
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    tutorial::ClusterSceneData geometry_;
    ComPtr<ID3D12DeviceRaytracing2> rtDevice_;
    ComPtr<ID3D12CommandListRaytracing2> rtCommands_;
    ComPtr<ID3D12Resource> restVertices_, restClusters_, clusterMetadata_, directArgs_, instantiateArgs_,
        dummyAddresses_;
    ComPtr<ID3D12Resource> templateStorage_, templateAddresses_, templateArgs_, clasStorage_, clasAddresses_,
        clasScratch_, blasScratch_, tlasScratch_, instanceUpload_;
    std::vector<ComPtr<ID3D12Resource>> blasArguments_, blasDestinations_, retiredScratch_;
    ComPtr<ID3D12RootSignature> geometryRoot_;
    ComPtr<ID3D12PipelineState> geometryPipeline_;
    ComPtr<ID3D12QueryHeap> timestamps_;
    ComPtr<ID3D12Resource> timestampReadback_;
    uint64_t timestampFrequency_ = 0, clasBytes_ = 0, blasBytes_ = 0, scratchBytes_ = 0, templateBytes_ = 0;
    D3D12_RESOURCE_STATES verticesState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                          metadataState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                          directState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                          instantiateState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                          addressesState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    int geometryMode_ = 0, builtMode_ = -1, geometryView_ = 0;
    bool paused_ = false, step_ = false, forceGeometry_ = true;
    float animationTime_ = 0, lastUpdateSeconds_ = 0;
    ImVec2 stepPoint_{}, resetPoint_{};
    void DumpClusterBuffer(const std::filesystem::path &, ID3D12Resource *, D3D12_RESOURCE_STATES &);
    uint32_t renderFrame_ = 0;
    std::array<double, 6> gpuTimes_{}, initialBuildTimes_{};
    bool geometryBuiltThisFrame_ = false, topBuiltThisFrame_ = false;
    nlohmann::json timingFrames_ = nlohmann::json::array();
    void BeginBuild();
    void EndBuild();
    void Uav(ID3D12Resource *);
    void Stamp(UINT);
    void CreateClusterResources();
    void PrepareTemplates();
    void GenerateGeometry();
    void CollectClusterTimings();
    void DrawClusterPanel();
    D3D12_RTAS_CLUSTER_LIMITS ClusterLimits(bool) const;

    tutorial::RaySceneData rayData_;
    std::vector<tutorial::RayInstance> rayInstances_;
    ComPtr<ID3D12Resource> rayVertices_, rayIndices_, rayPrimitives_, rayMaterials_, rayInstancesBuffer_,
        tlas_, shaderTable_;
    std::vector<ComPtr<ID3D12Resource>> blas_;
    std::array<ComPtr<ID3D12Resource>, 2> diagnostic_;
    std::array<D3D12_RESOURCE_STATES, 2> diagnosticStates_{};
    ComPtr<ID3D12StateObject> rayPipeline_;
    int builtScene_ = -1, builtPreset_ = -1;
    float builtOccluder_ = 100;
    ComPtr<ID3D12Resource> DefaultBuffer(uint64_t, D3D12_RESOURCE_STATES);
    void BuildBlas();
    void BuildTlas();
    void CreateRayPipeline();
    HWND window_ = nullptr;
    uint32_t width_ = 0, height_ = 0, rtvStride_ = 0, renderedBuffer_ = 0, fontSlot_ = 0;
    tutorial::Workshop workshop_;
    tutorial::RaytracingPanel panel_;
    std::string gpuName_;
    tutorial::Options options_;
    ComPtr<IDXGIFactory6> factory_;
    ComPtr<ID3D12Device16> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList4> commands_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12InfoQueue> infoQueue_;
    uint64_t fenceValue_ = 0;
    HANDLE fenceEvent_ = nullptr;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_, textureHeap_, postHeap_;
    std::array<ComPtr<ID3D12Resource>, 2> targets_;
    std::array<ComPtr<ID3D12Resource>, 4> hdr_;
    std::array<D3D12_RESOURCE_STATES, 4> hdrStates_{};
    ComPtr<ID3D12Resource> brdf_, points_, constants_, postConstants_;
    std::array<ComPtr<ID3D12Resource>, 3> environment_;
    std::vector<ComPtr<ID3D12Resource>> textures_;
    void *constantsData_ = nullptr, *postData_ = nullptr;
    ComPtr<ID3D12RootSignature> rootSignature_, postRoot_;
    ComPtr<ID3D12PipelineState> postPipeline_;
    std::array<ComPtr<ID3D12PipelineState>, 2> bloomPipelines_;
    ComPtr<ID3D12Resource> UploadBuffer(const void *, uint64_t);
    ComPtr<ID3D12Resource> LoadDdsTexture(const std::filesystem::path &);
    void WaitForGpu();
    void ReadValidationMessages();
    void CreateTargets();
    void CreateScene();
    void CreateSizeResources();
    void UpdateDescriptors();
    void CreatePipeline();
    void SetPost(UINT pass, bool compute);
    void DumpGpuData(const std::filesystem::path &);
    void Move(ID3D12Resource *, D3D12_RESOURCE_STATES &, D3D12_RESOURCE_STATES);
    void CreateImGui();
    void CreateUiPipeline();
    void RenderImGui();
    void DestroyImGui() noexcept;
    bool win32Ui_ = false;
    ComPtr<ID3D12Resource> uiFont_, uiVertices_, uiIndices_, uiConstants_;
    ComPtr<ID3D12RootSignature> uiRoot_;
    ComPtr<ID3D12PipelineState> uiPipeline_;
    size_t uiVertexCapacity_ = 0, uiIndexCapacity_ = 0;
};
