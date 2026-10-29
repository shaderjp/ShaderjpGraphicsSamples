#pragma once
#include "../../Shared/BindlessPanel.h"
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
    uint32_t width_ = 0, height_ = 0, rtvStride_ = 0, renderedBuffer_ = 0, fontSlot_ = 0, descriptorStride_ = 0;
    uint32_t imageCount_ = 0, materialBase_ = 0, bufferBase_ = 0, geometryBase_ = 0, uavBase_ = 0, gbufferBase_ = 0,
             displayBase_ = 0;
    tutorial::Options options_, initialOptions_;
    tutorial::GalleryData data_;
    tutorial::GalleryPanel panel_;
    tutorial::GalleryTimings timing_;
    tutorial::GalleryConstants frame_{};
    ComPtr<IDXGIFactory6> factory_;
    ComPtr<ID3D12Device16> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList6> commands_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12InfoQueue> infoQueue_;
    uint64_t fenceValue_ = 0, timestampFrequency_ = 0;
    HANDLE fenceEvent_ = nullptr;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_, depthHeap_, textureHeap_;
    std::array<ComPtr<ID3D12Resource>, 2> targets_;
    std::array<ComPtr<ID3D12Resource>, 5> images_;
    std::array<D3D12_RESOURCE_STATES, 5> imageStates_{};
    ComPtr<ID3D12Resource> depth_, constants_, visibility_, indirect_, timestampReadback_;
    ComPtr<ID3D12QueryHeap> timestamps_;
    std::vector<ComPtr<ID3D12Resource>> buffers_, textures_, blas_;
    ComPtr<ID3D12Resource> tlas_, shaderTable_;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12PipelineState> raster_, mesh_, display_, light_, reset_, cull_, finalize_;
    ComPtr<ID3D12PipelineState> countReset_, countVisible_;
    ComPtr<ID3D12CommandSignature> indirectSignature_;
    ComPtr<ID3D12StateObject> rayPipeline_;
    void CreateTargets();
    void CreateScene();
    void CreateSizeResources();
    void UpdateDescriptors();
    void CreatePipeline();
    void BuildAcceleration();
    void CreateRayPipeline();
    void Bind(bool, uint32_t = 0);
    void Stamp(UINT);
    void WaitForGpu();
    void ReadValidationMessages();
    void Execute();
    void BeginCommands();
    void Move(ID3D12Resource *, D3D12_RESOURCE_STATES &, D3D12_RESOURCE_STATES);
    ComPtr<ID3D12Resource> UploadBuffer(const void *, uint64_t);
    ComPtr<ID3D12Resource> DefaultBuffer(uint64_t, D3D12_RESOURCE_STATES, D3D12_HEAP_TYPE = D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> LoadTexture(const std::vector<uint32_t> &, uint32_t);
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT) const;
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(UINT) const;
    void DumpGpuData(const std::filesystem::path &);
    void CollectStatistics();
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
