#pragma once
#include "../../Shared/DeferredPanel.h"
#include "../../Shared/PostProcessData.h"
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
    tutorial::DeferredPanel panel_;
    tutorial::DeferredTiming timing_;
    tutorial::Options options_;
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
    ComPtr<ID3D12Device16> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> commands_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12InfoQueue> infoQueue_;
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
