#pragma once
#include "../../Shared/MeshTiming.h"
#include "../../Shared/PostProcessData.h"
#include "../../Shared/SkinningPanel.h"
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
    HWND window_ = nullptr;
    uint32_t width_ = 0, height_ = 0, rtvStride_ = 0, renderedBuffer_ = 0, fontSlot_ = 0;
    tutorial::Workshop workshop_;
    tutorial::SkinningData skinning_;
    void CreateSkinResources();
    void UpdateSkinBuffers();
    void DumpSkinningData(const std::filesystem::path &);
    tutorial::SkinningPanel panel_;
    tutorial::SkinningTiming timing_;
    double meshCpuFrame_ = 0;
    void PrepareMeshIndirect();
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
    ComPtr<ID3D12Resource> gridConstants_, gridStatsReadback_;
    void *gridConstantsData_ = nullptr;
    std::array<ComPtr<ID3D12Resource>, 4> gridBuffers_;
    std::array<D3D12_RESOURCE_STATES, 4> gridStates_{};
    ComPtr<ID3D12PipelineState> tileDepthPipeline_, cullPipeline_;
    void CreateLightGridResources();
    void BuildLightLists();
    void UpdateLightGridTestState();
    void CaptureLightGridTestState();
    void CopyGridStatistics();
    void ReadGridStatistics();
    void DumpLightGrid(const std::filesystem::path &);
    ComPtr<IDXGIFactory6> factory_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> commands_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12InfoQueue> infoQueue_;

    std::array<ComPtr<ID3D12Resource>, 10> meshBuffers_;
    std::array<ComPtr<ID3D12Resource>, 3> skinBuffers_;
    ComPtr<ID3D12Resource> skinCache_;
    ComPtr<ID3D12PipelineState> skinCompute_;
    D3D12_RESOURCE_STATES skinCacheState_ = D3D12_RESOURCE_STATE_COMMON;
    ComPtr<ID3D12Resource> meshVisible_, meshArgs_, meshReadback_;
    D3D12_RESOURCE_STATES meshVisibleState_ = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES meshArgsState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ComPtr<ID3D12GraphicsCommandList6> meshCommands_;
    ComPtr<ID3D12RootSignature> meshRoot_;
    ComPtr<ID3D12CommandSignature> meshSignature_;
    std::array<ComPtr<ID3D12PipelineState>, 6> meshDirect_, meshTask_;
    ComPtr<ID3D12PipelineState> meshReset_, meshCull_, meshFinalize_;
    uint32_t meshSlot_ = 0;
    void BindMesh(int, UINT, UINT, UINT, bool);
    uint64_t fenceValue_ = 0, timestampFrequency_ = 0;
    HANDLE fenceEvent_ = nullptr;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_, textureHeap_, sceneRtv_, sceneDsv_, postHeap_;
    std::array<ComPtr<ID3D12Resource>, 2> targets_;
    std::array<ComPtr<ID3D12Resource>, 5> gbuffer_;
    std::array<D3D12_RESOURCE_STATES, 5> gStates_{};
    std::array<ComPtr<ID3D12Resource>, 4> hdr_;
    std::array<D3D12_RESOURCE_STATES, 4> hdrStates_{};
    ComPtr<ID3D12Resource> shadow_, brdf_, points_, constants_, postConstants_, queryReadback_;
    std::array<ComPtr<ID3D12Resource>, 3> environment_;
    std::vector<ComPtr<ID3D12Resource>> textures_;
    D3D12_RESOURCE_STATES shadowState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    struct Geometry
    {
        ComPtr<ID3D12Resource> vertices, indices;
        D3D12_VERTEX_BUFFER_VIEW vb{};
        D3D12_INDEX_BUFFER_VIEW ib{};
        UINT count = 0;
    };
    std::vector<Geometry> geometry_;
    void *constantsData_ = nullptr, *postData_ = nullptr;
    ComPtr<ID3D12RootSignature> rootSignature_, postRoot_;
    std::array<ComPtr<ID3D12PipelineState>, 2> gPipelines_, forwardPipelines_, transparentPipelines_,
        shadowPipelines_;
    ComPtr<ID3D12PipelineState> lightingPixel_, lightingCompute_, skyPipeline_, postPipeline_, testPipeline_;
    std::array<ComPtr<ID3D12PipelineState>, 2> bloomPipelines_;
    ComPtr<ID3D12QueryHeap> queries_;
    ComPtr<ID3D12Resource> UploadBuffer(const void *, uint64_t);
    ComPtr<ID3D12Resource> LoadDdsTexture(const std::filesystem::path &);
    void WaitForGpu();
    void ReadValidationMessages();
    void CreateTargets();
    void CreateScene();
    void CreateSizeResources();
    void UpdateDescriptors();
    void CreatePipeline();
    void Bind(UINT object, UINT material, bool compute = false);
    void DrawObjects(int pass);
    void SetPost(UINT pass, bool compute);
    void DumpGpuData(const std::filesystem::path &);
    void Move(ID3D12Resource *, D3D12_RESOURCE_STATES &, D3D12_RESOURCE_STATES);
    void Stamp(UINT index);
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
