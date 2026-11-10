#include "Sample.h"
#include <imgui_impl_win32.h>
// Declared explicitly by the platform backend to avoid pulling Windows headers into imgui.h.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
using namespace tutorial;

// Agility SDK selected by Build/Common.props. The DLLs live in a separate directory next to the exe.
extern "C"
{
    __declspec(dllexport) extern const UINT D3D12SDKVersion = TUTORIAL_D3D12_SDK_VERSION;
    __declspec(dllexport) extern const char *D3D12SDKPath = ".\\D3D12\\";
}
namespace
{
D3D12_RESOURCE_BARRIER Transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                                  D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    return barrier;
}
D3D12_RESOURCE_DESC BufferDescription(uint64_t bytes)
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = bytes;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}
} // namespace
void Sample::OnInit(HWND window, uint32_t width, uint32_t height, const Options &options)
{
    panel_.Initialize(options);
    window_ = window;
    width_ = width;
    height_ = height;
    UINT factoryFlags = 0;
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    CheckHR(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),
            "D3D12 Debug Layer (install Graphics Tools if unavailable)");
    debug->EnableDebugLayer();
    factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    CheckHR(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_)), "CreateDXGIFactory2");
    for (UINT index = 0;; ++index)
    {
        ComPtr<IDXGIAdapter1> adapter;
        const auto result = factory_->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                 IID_PPV_ARGS(&adapter));
        if (result == DXGI_ERROR_NOT_FOUND)
            break;
        CheckHR(result, "EnumAdapterByGpuPreference");
        DXGI_ADAPTER_DESC1 description{};
        adapter->GetDesc1(&description);
        if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_))))
            break;
    }
    if (!device_)
        throw std::runtime_error("A hardware D3D12 feature level 12_0 device is required");
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_6};
    CheckHR(device_->CheckFeatureSupport1(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel),
                                          TUTORIAL_D3D12_SDK_VERSION),
            "Shader Model query");
    if (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_6)
        throw std::runtime_error("Shader Model 6.6 is required; update your GPU driver");
#ifdef _DEBUG
    CheckHR(device_.As(&infoQueue_), "ID3D12InfoQueue");
    Trace("D3D12 Debug Layer enabled");
#endif
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    CheckHR(device_->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue_)), "CreateCommandQueue");
    DXGI_SWAP_CHAIN_DESC1 swapDescription{};
    swapDescription.Width = width;
    swapDescription.Height = height;
    swapDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDescription.SampleDesc.Count = 1;
    swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDescription.BufferCount = 2;
    swapDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swapchain;
    CheckHR(factory_->CreateSwapChainForHwnd(queue_.Get(), window, &swapDescription, nullptr, nullptr,
                                             &swapchain),
            "CreateSwapChainForHwnd");
    CheckHR(swapchain.As(&swapchain_), "IDXGISwapChain3");
    CheckHR(factory_->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation");
    D3D12_DESCRIPTOR_HEAP_DESC rtvDescription{};
    rtvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDescription.NumDescriptors = 2;
    CheckHR(device_->CreateDescriptorHeap(&rtvDescription, IID_PPV_ARGS(&rtvHeap_)), "Create RTV heap");
    rtvStride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CheckHR(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)),
            "CreateCommandAllocator");
    CheckHR(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr,
                                       IID_PPV_ARGS(&commands_)),
            "CreateCommandList");
    CheckHR(commands_->Close(), "Close initial command list");
    CheckHR(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_)
        throw std::runtime_error("CreateEventW failed");
    CreateTargets();
    CreateResources();
    CreatePipeline();
    CreateImGui();
    ReadValidationMessages();
}

bool Sample::OnMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LRESULT &result)
{
    if (!ImGui::GetCurrentContext() || !win32Ui_)
        return false;
    result = ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
    const auto &io = ImGui::GetIO();
    return result != 0 || (io.WantCaptureKeyboard &&
                           (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN ||
                            message == WM_SYSKEYUP || message == WM_CHAR));
}

void Sample::CreateTargets()
{
    auto handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    for (UINT index = 0; index < 2; ++index)
    {
        CheckHR(swapchain_->GetBuffer(index, IID_PPV_ARGS(&targets_[index])), "Swapchain GetBuffer");
        device_->CreateRenderTargetView(targets_[index].Get(), &view, handle);
        handle.ptr += rtvStride_;
    }
}

void Sample::WaitForGpu()
{
    const auto value = ++fenceValue_;
    CheckHR(queue_->Signal(fence_.Get(), value), "Queue Signal");
    if (fence_->GetCompletedValue() < value)
    {
        CheckHR(fence_->SetEventOnCompletion(value, fenceEvent_), "SetEventOnCompletion");
        if (WaitForSingleObject(fenceEvent_, 30000) != WAIT_OBJECT_0)
            throw std::runtime_error("GPU fence wait failed or timed out");
    }
}

void Sample::ReadValidationMessages()
{
    if (!infoQueue_)
        return;
    const auto count = infoQueue_->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (uint64_t index = 0; index < count; ++index)
    {
        SIZE_T bytes = 0;
        CheckHR(infoQueue_->GetMessage(index, nullptr, &bytes), "InfoQueue message size");
        std::vector<uint8_t> storage(bytes);
        auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
        CheckHR(infoQueue_->GetMessage(index, message, &bytes), "InfoQueue GetMessage");
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
            ValidationError(message->pDescription);
        else if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING)
            Trace(std::string("D3D12 warning: ") + message->pDescription);
    }
    infoQueue_->ClearStoredMessages();
}

void Sample::OnScreenshot(const std::filesystem::path &path)
{
    auto *target = targets_[renderedBuffer_].Get();
    const auto description = target->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    device_->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    const auto bufferDescription = BufferDescription(bytes);
    ComPtr<ID3D12Resource> readback;
    CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDescription,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&readback)),
            "Screenshot readback buffer");
    CheckHR(allocator_->Reset(), "Screenshot allocator Reset");
    CheckHR(commands_->Reset(allocator_.Get(), nullptr), "Screenshot command Reset");
    auto before = Transition(target, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands_->ResourceBarrier(1, &before);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = target;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    commands_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    auto after = Transition(target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    commands_->ResourceBarrier(1, &after);
    CheckHR(commands_->Close(), "Screenshot Close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitForGpu();
    void *mapped = nullptr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
    CheckHR(readback->Map(0, &range, &mapped), "Screenshot Map");
    SavePng(path, width_, height_, static_cast<const uint8_t *>(mapped) + footprint.Offset,
            footprint.Footprint.RowPitch);
    D3D12_RANGE written{0, 0};
    readback->Unmap(0, &written);
    ReadValidationMessages();
    if (panel_.captureGpu)
        CaptureData(path);
}

Sample::ComPtr<ID3D12Resource> Sample::UploadBuffer(const void *data, uint64_t bytes)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    const auto description = BufferDescription(bytes);
    ComPtr<ID3D12Resource> resource;
    CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&resource)),
            "Create upload buffer");
    if (data)
    {
        void *mapped = nullptr;
        D3D12_RANGE read{0, 0};
        CheckHR(resource->Map(0, &read, &mapped), "Upload buffer Map");
        std::memcpy(mapped, data, static_cast<size_t>(bytes));
        resource->Unmap(0, nullptr);
    }
    return resource;
}

void Sample::OnUpdate(float)
{
    if (const auto path = panel_.TestCapture(); !path.empty())
        OnScreenshot(path);
    ImGui_ImplWin32_NewFrame();
    panel_.QueueTestInput();
    ImGui::NewFrame();
    panel_.Draw(width_, height_, "Direct3D12");
    ImGui::Render();
}

D3D12_CPU_DESCRIPTOR_HANDLE Sample::Rtv(UINT slot) const
{
    auto h = lessonRtv_->GetCPUDescriptorHandleForHeapStart();
    h.ptr += slot * rtvStride_;
    return h;
}
D3D12_CPU_DESCRIPTOR_HANDLE Sample::Dsv(UINT slot) const
{
    auto h = lessonDsv_->GetCPUDescriptorHandleForHeapStart();
    h.ptr += slot * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    return h;
}
void Sample::Bind(UINT slot)
{
    commands_->SetGraphicsRootSignature(rootSignature_.Get());
    commands_->SetGraphicsRootConstantBufferView(0, constants_->GetGPUVirtualAddress() + 512 * slot);
    ID3D12DescriptorHeap *heaps[] = {textureHeap_.Get()};
    commands_->SetDescriptorHeaps(1, heaps);
    commands_->SetGraphicsRootDescriptorTable(1, textureHeap_->GetGPUDescriptorHandleForHeapStart());
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
void Sample::CreateResources()
{
    using namespace tutorial::depth;
    panel_.Layout(width_, height_);
    sceneWidth_ = std::max(1u, UINT(panel_.cells[0].w));
    sceneHeight_ = std::max(1u, UINT(panel_.cells[0].h));
    shadowSize_ = panel_.shadow ? panel_.settings.resolution : 1;
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT,
                                   DXGI_FORMAT_D32_FLOAT};
    const DXGI_FORMAT srvs[] = {DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
                                DXGI_FORMAT_R32_FLOAT};
    for (int i = 0; i < 3; ++i)
    {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT d{formats[i]}, s{srvs[i]};
        panel_.supported[i] = SUCCEEDED(device_->CheckFeatureSupport1(
                                  D3D12_FEATURE_FORMAT_SUPPORT, &d, sizeof(d), TUTORIAL_D3D12_SDK_VERSION)) &&
                              SUCCEEDED(device_->CheckFeatureSupport1(
                                  D3D12_FEATURE_FORMAT_SUPPORT, &s, sizeof(s), TUTORIAL_D3D12_SDK_VERSION)) &&
                              (d.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) &&
                              (s.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD);
    }
    if (!panel_.supported[2] || !panel_.supported[panel_.settings.format])
        throw std::runtime_error("Requested depth format lacks depth-attachment / shader-load support");
    if (!textureHeap_)
    {
        D3D12_DESCRIPTOR_HEAP_DESC h{};
        h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        h.NumDescriptors = 9;
        h.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        CheckHR(device_->CreateDescriptorHeap(&h, IID_PPV_ARGS(&textureHeap_)), "Depth SRV heap");
        h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        h.NumDescriptors = 8;
        h.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        CheckHR(device_->CreateDescriptorHeap(&h, IID_PPV_ARGS(&lessonRtv_)), "Lesson RTV heap");
        h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        h.NumDescriptors = 3;
        CheckHR(device_->CreateDescriptorHeap(&h, IID_PPV_ARGS(&lessonDsv_)), "Lesson DSV heap");
        constants_ = UploadBuffer(nullptr, 512 * 16);
        D3D12_RANGE read{0, 0};
        CheckHR(constants_->Map(0, &read, &constantsData_), "Depth constants Map");
    }
    const DXGI_FORMAT resources[] = {DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R24G8_TYPELESS,
                                     DXGI_FORMAT_R32_TYPELESS};
    auto create = [&](ComPtr<ID3D12Resource> &resource, UINT w, UINT h, DXGI_FORMAT format, bool depth,
                      DXGI_FORMAT viewFormat, float clear)
    {
        resource.Reset();
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w;
        d.Height = h;
        d.DepthOrArraySize = d.MipLevels = 1;
        d.Format = format;
        d.SampleDesc.Count = 1;
        d.Flags = depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE cv{};
        cv.Format = viewFormat;
        if (depth)
            cv.DepthStencil.Depth = clear;
        else
            cv.Color[0] = clear;
        CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv,
                                                 IID_PPV_ARGS(&resource)),
                "Create depth lesson image");
    };
    const int selected = panel_.shadow ? 2 : panel_.settings.format;
    for (int i = 0; i < 8; ++i)
    {
        bool dep = i == 0 || i == 3 || i == 6;
        int f = i == 6 ? 2 : selected;
        UINT w = i >= 6 ? shadowSize_ : sceneWidth_, h = i >= 6 ? shadowSize_ : sceneHeight_;
        DXGI_FORMAT fmt = dep                  ? resources[f]
                          : i == 7             ? DXGI_FORMAT_R32_FLOAT
                          : (i == 1 || i == 4) ? DXGI_FORMAT_R32G32B32A32_FLOAT
                                               : DXGI_FORMAT_R8G8B8A8_UNORM;
        create(textures_[i], w, h, fmt, dep, dep ? formats[f] : fmt,
               dep      ? ((i == 3 || panel_.shadow) && i != 6 ? 0.f : 1.f)
               : i == 7 ? -1.f
                        : 0.f);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = dep ? srvs[f] : fmt;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        auto handle = textureHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += i * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        device_->CreateShaderResourceView(textures_[i].Get(), &srv, handle);
        if (dep)
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
            dsv.Format = formats[f];
            dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            device_->CreateDepthStencilView(textures_[i].Get(), &dsv, Dsv(i / 3));
        }
        else
            device_->CreateRenderTargetView(textures_[i].Get(), nullptr,
                                            Rtv(i == 2   ? 0
                                                : i == 1 ? 1
                                                : i == 5 ? 2
                                                : i == 4 ? 3
                                                         : 4));
    }
    for (int i = 0; i < 3; ++i)
    {
        create(exports_[i], i == 2 ? shadowSize_ : sceneWidth_, i == 2 ? shadowSize_ : sceneHeight_,
               DXGI_FORMAT_R32G32B32A32_FLOAT, false, DXGI_FORMAT_R32G32B32A32_FLOAT, 0);
        device_->CreateRenderTargetView(exports_[i].Get(), nullptr, Rtv(5 + i));
    }
    activeFormat_ = panel_.settings.format;
    activeResolution_ = panel_.settings.resolution;
    panel_.dirty = true;
}
void Sample::CreatePipeline()
{
    if (!rootSignature_)
    {
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, 0};
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable = {1, &range};
        D3D12_ROOT_SIGNATURE_DESC root{};
        root.NumParameters = 2;
        root.pParameters = params;
        root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> blob, error;
        CheckHR(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                "Depth root serialize");
        CheckHR(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                             IID_PPV_ARGS(&rootSignature_)),
                "Depth root");
    }
    auto vs = ReadBinary(AssetPath(L"Shaders/DepthVertex.cso")),
         ps = ReadBinary(AssetPath(L"Shaders/DepthPixel.cso")),
         sv = ReadBinary(AssetPath(L"Shaders/DepthFullscreen.cso")),
         sp = ReadBinary(AssetPath(L"Shaders/ShadowLinear.cso")),
         dp = ReadBinary(AssetPath(L"Shaders/DepthDisplay.cso"));
    D3D12_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = rootSignature_.Get();
    d.VS = {vs.data(), vs.size()};
    d.PS = {ps.data(), ps.size()};
    d.InputLayout = {elements, 3};
    d.SampleMask = UINT_MAX;
    d.SampleDesc.Count = 1;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.BlendState.IndependentBlendEnable = TRUE;
    for (auto &rt : d.BlendState.RenderTarget)
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.DepthStencilState.DepthEnable = TRUE;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 2;
    d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT,
                                   DXGI_FORMAT_D32_FLOAT};
    d.DSVFormat = formats[panel_.shadow ? 2 : panel_.settings.format];
    for (int i = 0; i < 2; ++i)
    {
        scenePipelines_[i].Reset();
        d.DepthStencilState.DepthFunc =
            i || panel_.shadow ? D3D12_COMPARISON_FUNC_GREATER : D3D12_COMPARISON_FUNC_LESS;
        CheckHR(device_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&scenePipelines_[i])),
                "Depth scene PSO");
    }
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
    d.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
    d.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    d.PS = {sp.data(), sp.size()};
    shadowPipeline_.Reset();
    CheckHR(device_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&shadowPipeline_)), "Linear shadow PSO");
    d.InputLayout = {};
    d.VS = {sv.data(), sv.size()};
    d.PS = {dp.data(), dp.size()};
    d.DepthStencilState.DepthEnable = FALSE;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    d.DSVFormat = DXGI_FORMAT_UNKNOWN;
    d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    displayPipeline_.Reset();
    CheckHR(device_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&displayPipeline_)), "Depth display PSO");
    d.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    exportPipeline_.Reset();
    CheckHR(device_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&exportPipeline_)), "Depth readback PSO");
}
void Sample::RenderLessons()
{
    auto barrier = [&](int slot, bool writing)
    {
        auto b = Transition(
            textures_[slot].Get(),
            writing ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                    : (slot % 3 == 0 ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET),
            writing ? (slot % 3 == 0 ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET)
                    : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commands_->ResourceBarrier(1, &b);
    };
    auto viewport = [&](UINT w, UINT h)
    {
        D3D12_VIEWPORT v{0, 0, float(w), float(h), 0, 1};
        D3D12_RECT r{0, 0, LONG(w), LONG(h)};
        commands_->RSSetViewports(1, &v);
        commands_->RSSetScissorRects(1, &r);
    };
    commands_->IASetVertexBuffers(0, 1, &vertexView_);
    barrier(6, true);
    barrier(7, true);
    auto shadow = Rtv(4), sd = Dsv(2);
    float empty[4] = {-1, 0, 0, 0};
    commands_->ClearRenderTargetView(shadow, empty, 0, nullptr);
    commands_->ClearDepthStencilView(sd, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
    commands_->OMSetRenderTargets(1, &shadow, FALSE, &sd);
    viewport(shadowSize_, shadowSize_);
    Bind(2);
    commands_->SetPipelineState(shadowPipeline_.Get());
    if (panel_.shadow)
        commands_->DrawInstanced(drawCount_, 1, 0, 0);
    barrier(6, false);
    barrier(7, false);
    for (int p = 0; p < 2; ++p)
    {
        for (int i = 0; i < 3; ++i)
            barrier(p * 3 + i, true);
        D3D12_CPU_DESCRIPTOR_HANDLE colors[] = {Rtv(p * 2), Rtv(p * 2 + 1)};
        auto dep = Dsv(p);
        float clear[4] = {0, 0, 0, 0};
        for (auto c : colors)
            commands_->ClearRenderTargetView(c, clear, 0, nullptr);
        commands_->ClearDepthStencilView(dep, D3D12_CLEAR_FLAG_DEPTH, p || panel_.shadow ? 0.f : 1.f, 0, 0,
                                         nullptr);
        commands_->OMSetRenderTargets(2, colors, FALSE, &dep);
        viewport(sceneWidth_, sceneHeight_);
        Bind(p);
        commands_->SetPipelineState(scenePipelines_[p].Get());
        commands_->DrawInstanced(drawCount_, 1, 0, 0);
        for (int i = 0; i < 3; ++i)
            barrier(p * 3 + i, false);
    }
}
void Sample::OnRender()
{
    using namespace tutorial::depth;
    WaitForGpu();
    if (activeFormat_ != panel_.settings.format || activeResolution_ != panel_.settings.resolution)
    {
        CreateResources();
        CreatePipeline();
    }
    const auto vertices = Scene(panel_.settings, panel_.shadow);
    vertexBuffer_ = UploadBuffer(vertices.data(), vertices.size() * sizeof(Vertex));
    vertexView_ = {vertexBuffer_->GetGPUVirtualAddress(), UINT(vertices.size() * sizeof(Vertex)),
                   sizeof(Vertex)};
    drawCount_ = UINT(vertices.size());
    for (int i = 0; i < 16; ++i)
    {
        data_[i] = MakeConstants(panel_.settings, panel_.shadow, i % 2, sceneWidth_, sceneHeight_);
        if (i == 2)
        {
            data_[i].view = data_[i].lightView;
            data_[i].projection = data_[i].lightProjection;
        }
        if (i >= 3 && i < 9)
            data_[i].display = {float((i - 3) % 2), float((i - 3) / 2), data_[i].display.z,
                                data_[i].display.w};
        if (i >= 10)
            data_[i].display = {float(i == 11), float(i == 12 ? 4 : 3), data_[i].display.z,
                                data_[i].display.w};
        std::memcpy(static_cast<char *>(constantsData_) + 512 * i, &data_[i], sizeof(Constants));
    }
    CheckHR(allocator_->Reset(), "Depth allocator reset");
    CheckHR(commands_->Reset(allocator_.Get(), nullptr), "Depth commands reset");
    RenderLessons();
    UINT index = swapchain_->GetCurrentBackBufferIndex();
    auto before =
        Transition(targets_[index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands_->ResourceBarrier(1, &before);
    auto target = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    target.ptr += index * rtvStride_;
    const float clear[] = {.004f, .008f, .015f, 1};
    commands_->ClearRenderTargetView(target, clear, 0, nullptr);
    commands_->OMSetRenderTargets(1, &target, FALSE, nullptr);
    for (int i = 0; i < 6; ++i)
    {
        auto r = panel_.cells[i];
        D3D12_VIEWPORT viewport{r.x, r.y, r.w, r.h, 0, 1};
        D3D12_RECT scissor{LONG(r.x), LONG(r.y), LONG(r.x + r.w), LONG(r.y + r.h)};
        commands_->RSSetViewports(1, &viewport);
        commands_->RSSetScissorRects(1, &scissor);
        Bind(3 + i);
        commands_->SetPipelineState(displayPipeline_.Get());
        commands_->DrawInstanced(3, 1, 0, 0);
    }
    RenderImGui();
    auto after =
        Transition(targets_[index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    commands_->ResourceBarrier(1, &after);
    CheckHR(commands_->Close(), "Depth close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    CheckHR(swapchain_->Present(1, 0), "Depth present");
    WaitForGpu();
    renderedBuffer_ = index;
    if (panel_.dirty)
    {
        CaptureData();
        panel_.dirty = false;
    }
    ReadValidationMessages();
}
void Sample::CaptureData(const std::filesystem::path &path)
{
    WaitForGpu();
    CheckHR(allocator_->Reset(), "Readback allocator");
    CheckHR(commands_->Reset(allocator_.Get(), nullptr), "Readback commands");
    std::array<ComPtr<ID3D12Resource>, 3> buffers;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[3]{};
    UINT64 sizes[3]{};
    int count = panel_.shadow ? 3 : 2;
    for (int p = 0; p < count; ++p)
    {
        UINT w = p == 2 ? shadowSize_ : sceneWidth_, h = p == 2 ? shadowSize_ : sceneHeight_;
        auto begin = Transition(exports_[p].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                D3D12_RESOURCE_STATE_RENDER_TARGET);
        commands_->ResourceBarrier(1, &begin);
        auto rt = Rtv(5 + p);
        commands_->OMSetRenderTargets(1, &rt, FALSE, nullptr);
        D3D12_VIEWPORT v{0, 0, float(w), float(h), 0, 1};
        D3D12_RECT sc{0, 0, LONG(w), LONG(h)};
        commands_->RSSetViewports(1, &v);
        commands_->RSSetScissorRects(1, &sc);
        Bind(10 + p);
        commands_->SetPipelineState(exportPipeline_.Get());
        commands_->DrawInstanced(3, 1, 0, 0);
        auto copy = Transition(exports_[p].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
        commands_->ResourceBarrier(1, &copy);
        auto desc = exports_[p]->GetDesc();
        device_->GetCopyableFootprints(&desc, 0, 1, 0, &footprints[p], nullptr, nullptr, &sizes[p]);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        auto bd = BufferDescription(sizes[p]);
        CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&buffers[p])),
                "Depth readback buffer");
        D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
        src.pResource = exports_[p].Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = buffers[p].Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprints[p];
        commands_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        auto end = Transition(exports_[p].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commands_->ResourceBarrier(1, &end);
    }
    CheckHR(commands_->Close(), "Depth readback close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitForGpu();
    for (int p = 0; p < count; ++p)
    {
        UINT w = p == 2 ? shadowSize_ : sceneWidth_, h = p == 2 ? shadowSize_ : sceneHeight_;
        auto &output = p == 2 ? lightPixels_ : pixels_[p];
        output.resize(size_t(w) * h);
        void *mapped = nullptr;
        D3D12_RANGE read{0, SIZE_T(sizes[p])};
        CheckHR(buffers[p]->Map(0, &read, &mapped), "Depth readback map");
        for (UINT y = 0; y < h; ++y)
            std::memcpy(output.data() + size_t(y) * w,
                        static_cast<char *>(mapped) + footprints[p].Offset +
                            size_t(y) * footprints[p].Footprint.RowPitch,
                        size_t(w) * sizeof(tutorial::depth::Pixel));
        D3D12_RANGE written{0, 0};
        buffers[p]->Unmap(0, &written);
    }
    panel_.UpdateProbes(pixels_, sceneWidth_, sceneHeight_, lightPixels_);
    if (!path.empty())
        tutorial::depth::SaveMeasurements(path, panel_.settings, panel_.shadow, sceneWidth_, sceneHeight_,
                                          pixels_, lightPixels_, "Direct3D12");
}
void Sample::OnResize(uint32_t width, uint32_t height)
{
    if (width == width_ && height == height_)
        return;
    WaitForGpu();
    for (auto &t : targets_)
        t.Reset();
    CheckHR(swapchain_->ResizeBuffers(2, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0), "Depth resize");
    width_ = width;
    height_ = height;
    CreateTargets();
    CreateResources();
    ReadValidationMessages();
}
void Sample::DestroyLessons()
{
    for (auto &t : textures_)
        t.Reset();
    for (auto &t : exports_)
        t.Reset();
    for (auto &p : scenePipelines_)
        p.Reset();
    shadowPipeline_.Reset();
    displayPipeline_.Reset();
    exportPipeline_.Reset();
}
void Sample::OnDestroy() noexcept
{
    try
    {
        if (queue_ && fence_ && fenceEvent_)
            WaitForGpu();
        DestroyImGui();
        DestroyLessons();
        vertexBuffer_.Reset();
        if (constantsData_)
        {
            constants_->Unmap(0, nullptr);
            constantsData_ = nullptr;
        }
        constants_.Reset();
        rootSignature_.Reset();
        textureHeap_.Reset();
        lessonRtv_.Reset();
        lessonDsv_.Reset();
        ReadValidationMessages();
    }
    catch (const std::exception &e)
    {
        ValidationError(e.what());
    }
    DestroyImGui();
    for (auto &t : targets_)
        t.Reset();
    commands_.Reset();
    allocator_.Reset();
    swapchain_.Reset();
    rtvHeap_.Reset();
    queue_.Reset();
    fence_.Reset();
    infoQueue_.Reset();
    device_.Reset();
    factory_.Reset();
    if (fenceEvent_)
    {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}
