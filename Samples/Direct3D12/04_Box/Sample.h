#pragma once
#include "Application.h"
#include "Image.h"
#include "Mesh.h"
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

  private:
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    void CreateTargets();
    void WaitForGpu();
    void ReadValidationMessages();
    void CreatePipeline();
    void CreateScene(const tutorial::Options &options);
    ComPtr<ID3D12Resource> UploadBuffer(const void *data, uint64_t bytes);
    void CreateDepth();
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
};
