#pragma once
#include "../../Shared/PostProcessPanel.h"
#include "Application.h"
#include "DdsImage.h"
#include "Gltf.h"
#include "Image.h"
#include "Lighting.h"
#include <array>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

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
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    void CreatePostResources();
    void CreatePostTargets();
    void UpdatePostDescriptors();
    void DestroyPostResources() noexcept;
    void PostTransition(UINT, D3D12_RESOURCE_STATES);
    void SetPostBindings(UINT, UINT, UINT, bool);
    void DrawHdrScene(UINT, UINT);
    void DumpGpuData(const std::filesystem::path &);
    std::array<ComPtr<ID3D12Resource>, 5> postImages_;
    std::array<D3D12_RESOURCE_STATES, 5> postStates_{};
    ComPtr<ID3D12Resource> monitorDepth_, postConstants_, particles_;
    void *postData_ = nullptr;
    ComPtr<ID3D12DescriptorHeap> postRtv_, monitorDsv_, postHeap_;
    ComPtr<ID3D12RootSignature> postRoot_;
    ComPtr<ID3D12PipelineState> postPipeline_, testPipeline_, monitorPipeline_, particleCompute_,
        particlePipeline_;
    std::array<ComPtr<ID3D12PipelineState>, 3> bloomPipelines_;
    D3D12_VERTEX_BUFFER_VIEW particleView_{};
    bool particlesReady_ = false, captureOnly_ = false;
    float delta_ = 1.f / 60;
    uint32_t simulationSteps_ = 0;

    void CreateTargets();
    void WaitForGpu();
    void ReadValidationMessages();
    void CreatePipeline();
    void CreateScene(const tutorial::Options &options);
    ComPtr<ID3D12Resource> UploadBuffer(const void *data, uint64_t bytes);
    void CreateDepth();
    void LoadTexture(const std::filesystem::path &path, uint32_t slot, DXGI_FORMAT format);
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
    uint32_t width_ = 0, height_ = 0, rtvStride_ = 0, renderedBuffer_ = 0;
    ComPtr<IDXGIFactory6> factory_;
    ComPtr<ID3D12Device16> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    std::array<ComPtr<ID3D12Resource>, 2> targets_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> commands_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12InfoQueue> infoQueue_;
    uint64_t fenceValue_ = 0;
    HANDLE fenceEvent_ = nullptr;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipeline_;
    ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexView_{};
    uint32_t drawCount_ = 0;
    ComPtr<ID3D12Resource> indexBuffer_, constants_, depth_;
    ComPtr<ID3D12DescriptorHeap> depthHeap_;
    D3D12_INDEX_BUFFER_VIEW indexView_{};
    void *constantsData_ = nullptr;
    std::vector<ComPtr<ID3D12Resource>> textures_;
    struct LabObject
    {
        ComPtr<ID3D12Resource> vertices, indices;
        D3D12_VERTEX_BUFFER_VIEW vertexView{};
        D3D12_INDEX_BUFFER_VIEW indexView{};
        UINT count = 0;
    };
    std::vector<LabObject> objects_;
    std::array<std::array<ComPtr<ID3D12Resource>, 3>, 3> environments_;
    ComPtr<ID3D12Resource> brdf_, shadow_;
    ComPtr<ID3D12DescriptorHeap> shadowHeap_;
    ComPtr<ID3D12PipelineState> skyPipeline_, shadowPipeline_;
    ComPtr<ID3D12Resource> LoadDdsTexture(const std::filesystem::path &path);
    void UpdateLabDescriptors();
    ComPtr<ID3D12DescriptorHeap> textureHeap_;
    ComPtr<ID3D12Resource> uiFont_, uiVertices_, uiIndices_, uiConstants_;
    ComPtr<ID3D12RootSignature> uiRoot_;
    ComPtr<ID3D12PipelineState> uiPipeline_;
    size_t uiVertexCapacity_ = 0, uiIndexCapacity_ = 0;
};
