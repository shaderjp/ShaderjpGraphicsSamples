#pragma once
#include "../../Shared/DepthPanel.h"
#include "Application.h"
#include "Image.h"
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
    void CreateTargets();
    void WaitForGpu();
    void ReadValidationMessages();
    void CreatePipeline();
    ComPtr<ID3D12Resource> UploadBuffer(const void *data, uint64_t bytes);
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
    ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexView_{};
    uint32_t drawCount_ = 0;
    ComPtr<ID3D12Resource> constants_;
    void *constantsData_ = nullptr;

    std::array<ComPtr<ID3D12Resource>, 8> textures_;
    std::array<ComPtr<ID3D12Resource>, 3> exports_;
    ComPtr<ID3D12DescriptorHeap> lessonRtv_, lessonDsv_;
    std::array<ComPtr<ID3D12PipelineState>, 2> scenePipelines_;
    ComPtr<ID3D12PipelineState> shadowPipeline_, displayPipeline_, exportPipeline_;
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv(UINT slot) const;
    D3D12_CPU_DESCRIPTOR_HANDLE Dsv(UINT slot) const;
    void Bind(UINT constants);

    ComPtr<ID3D12DescriptorHeap> textureHeap_;
    ComPtr<ID3D12Resource> uiFont_, uiVertices_, uiIndices_, uiConstants_;
    ComPtr<ID3D12RootSignature> uiRoot_;
    ComPtr<ID3D12PipelineState> uiPipeline_;
    size_t uiVertexCapacity_ = 0, uiIndexCapacity_ = 0;
};
