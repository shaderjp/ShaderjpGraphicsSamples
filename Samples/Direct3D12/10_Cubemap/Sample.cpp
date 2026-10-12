#include "Sample.h"
#include <imgui_impl_win32.h>
// Declared explicitly by the platform backend to avoid pulling Windows headers into imgui.h.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
using namespace tutorial;

// Agility SDK 1.619.6. The DLLs live in a separate directory next to the exe.
extern "C"
{
    __declspec(dllexport) extern const UINT D3D12SDKVersion = 619;
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
    scene_ = LoadLightingScene(options.scene.empty() ? AssetPath(L"Assets/Scenes/LightingLab/Exhibition.json")
                                                     : options.scene);
    model_ = scene_.objects.front().model;
    panel_.Initialize(scene_, options, LAB_TECHNIQUE);
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
    CheckHR(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)),
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
    CreatePipeline();
    CreateScene(options);

    CreateImGui();
    ReadValidationMessages();
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
    CreateDepth();
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
void Sample::OnUpdate(float seconds)
{
    if (const auto path = panel_.TestCapture(); !path.empty())
        OnScreenshot(path);
    ImGui_ImplWin32_NewFrame();
    panel_.QueueTestInput();
    ImGui::NewFrame();
    lastSeconds_ = seconds;
    panel_.Draw(width_, height_, model_, "Direct3D12");
    ImGui::Render();
}

void Sample::OnRender()
{
    // Fence completion precedes EVERY mapped constant/vertex/UI/descriptor write.
    WaitForGpu();
    UpdateLabDescriptors();
    for (size_t i = 0; i < objects_.size(); ++i)
    {
        auto c =
            MakeLabConstants(width_ - panel_.SceneLeft(width_), height_, scene_, i, LAB_TECHNIQUE,
                             panel_.view, panel_.environmentIntensity, panel_.yaw, panel_.directIntensity,
                             panel_.exposure, panel_.shadows, panel_.pcf, panel_.bias, panel_.occluder);
        c.lighting.shading = {float(width_ - panel_.SceneLeft(width_)), float(height_),
                              float(panel_.SceneLeft(width_)), 0};
        std::memcpy(static_cast<uint8_t *>(constantsData_) + i * 512, &c, sizeof(c));
    }
    CheckHR(allocator_->Reset(), "Frame allocator");
    CheckHR(commands_->Reset(allocator_.Get(), pipeline_.Get()), "Frame commands");
    commands_->SetGraphicsRootSignature(rootSignature_.Get());
    ID3D12DescriptorHeap *heaps[] = {textureHeap_.Get()};
    commands_->SetDescriptorHeaps(1, heaps);
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto draw = [&](size_t i) {
        auto &o = objects_[i];
        commands_->SetGraphicsRootConstantBufferView(0, constants_->GetGPUVirtualAddress() + i * 512);
        auto table = textureHeap_->GetGPUDescriptorHandleForHeapStart();
        table.ptr +=
            i * 9 * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        commands_->SetGraphicsRootDescriptorTable(1, table);
        commands_->IASetVertexBuffers(0, 1, &o.vertexView);
        commands_->IASetIndexBuffer(&o.indexView);
        commands_->DrawIndexedInstanced(o.count, 1, 0, 0, 0);
    };
    if constexpr (LAB_TECHNIQUE == 13)
    {
        auto b = Transition(shadow_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_DEPTH_WRITE);
        commands_->ResourceBarrier(1, &b);
        auto dsv = shadowHeap_->GetCPUDescriptorHandleForHeapStart();
        commands_->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
        commands_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
        D3D12_VIEWPORT v{0, 0, 2048, 2048, 0, 1};
        D3D12_RECT scissor{0, 0, 2048, 2048};
        commands_->RSSetViewports(1, &v);
        commands_->RSSetScissorRects(1, &scissor);
        commands_->SetPipelineState(shadowPipeline_.Get());
        for (size_t i = 0; i < objects_.size(); ++i)
            if (scene_.objects[i].castsShadow)
                draw(i);
        b = Transition(shadow_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commands_->ResourceBarrier(1, &b);
    }
    UINT index = swapchain_->GetCurrentBackBufferIndex();
    auto b =
        Transition(targets_[index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands_->ResourceBarrier(1, &b);
    auto target = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    target.ptr += index * rtvStride_;
    auto depth = depthHeap_->GetCPUDescriptorHandleForHeapStart();
    commands_->OMSetRenderTargets(1, &target, FALSE, &depth);
    float clear[] = {.025f, .035f, .06f, 1};
    commands_->ClearRenderTargetView(target, clear, 0, nullptr);
    commands_->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
    D3D12_VIEWPORT viewport{
        float(panel_.SceneLeft(width_)), 0, float(width_ - panel_.SceneLeft(width_)), float(height_), 0, 1};
    D3D12_RECT scissor{static_cast<LONG>(panel_.SceneLeft(width_)), 0, static_cast<LONG>(width_),
                       static_cast<LONG>(height_)};
    commands_->RSSetViewports(1, &viewport);
    commands_->RSSetScissorRects(1, &scissor);
    commands_->SetPipelineState(skyPipeline_.Get());
    commands_->SetGraphicsRootConstantBufferView(0, constants_->GetGPUVirtualAddress());
    commands_->SetGraphicsRootDescriptorTable(1, textureHeap_->GetGPUDescriptorHandleForHeapStart());
    commands_->DrawInstanced(3, 1, 0, 0);
    commands_->SetPipelineState(pipeline_.Get());
    for (size_t i = 0; i < objects_.size(); ++i)
        draw(i);
    if (panel_.visible)
    {
        commands_->OMSetRenderTargets(1, &target, FALSE, nullptr);
        RenderImGui();
    }
    b = Transition(targets_[index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    commands_->ResourceBarrier(1, &b);
    CheckHR(commands_->Close(), "Frame close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    CheckHR(swapchain_->Present(1, 0), "Present");
    WaitForGpu();
    renderedBuffer_ = index;
    ReadValidationMessages();
}

void Sample::OnResize(uint32_t width, uint32_t height)
{
    if (width == width_ && height == height_)
        return;
    WaitForGpu();
    for (auto &target : targets_)
        target.Reset();
    depth_.Reset();
    CheckHR(swapchain_->ResizeBuffers(2, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0), "ResizeBuffers");
    width_ = width;
    height_ = height;
    CreateTargets();
    ReadValidationMessages();
}
// Optional GPU readback captures the actual rendered image, including the clear-only sample.
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
}
void Sample::OnDestroy() noexcept
{
    try
    {
        if (queue_ && fence_ && fenceEvent_)
            WaitForGpu();
        DestroyImGui();
        vertexBuffer_.Reset();
        pipeline_.Reset();
        rootSignature_.Reset();
        if (constantsData_)
        {
            constants_->Unmap(0, nullptr);
            constantsData_ = nullptr;
        }
        constants_.Reset();
        indexBuffer_.Reset();
        depth_.Reset();
        depthHeap_.Reset();
        for (auto &texture : textures_)
            texture.Reset();
        objects_.clear();
        for (auto &e : environments_)
            for (auto &t : e)
                t.Reset();
        brdf_.Reset();
        shadow_.Reset();
        shadowHeap_.Reset();
        skyPipeline_.Reset();
        shadowPipeline_.Reset();
        textureHeap_.Reset();
        ReadValidationMessages();
    }
    catch (const std::exception &error)
    {
        ValidationError(error.what());
    }
    DestroyImGui(); // Also handles an exception during partial initialization.
    for (auto &target : targets_)
        target.Reset();
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

void Sample::CreateDepth()
{
    if (!depthHeap_)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        heap.NumDescriptors = 1;
        CheckHR(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&depthHeap_)), "Create DSV heap");
    }
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width_;
    description.Height = height_;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_D32_FLOAT;
    description.SampleDesc.Count = 1;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = description.Format;
    clear.DepthStencil.Depth = 1.0f;
    CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                             D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth_)),
            "Create depth texture");
    device_->CreateDepthStencilView(depth_.Get(), nullptr, depthHeap_->GetCPUDescriptorHandleForHeapStart());
}

void Sample::LoadTexture(const std::filesystem::path &path, uint32_t slot, DXGI_FORMAT format)
{
    const auto image = LoadWicImage(path);
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = image.width;
    description.Height = image.height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&textures_[slot])),
            "Create texture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    device_->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    auto upload = UploadBuffer(nullptr, bytes);
    void *mapped = nullptr;
    D3D12_RANGE read{0, 0};
    CheckHR(upload->Map(0, &read, &mapped), "Texture upload Map");
    for (UINT row = 0; row < image.height; ++row)
        std::memcpy(static_cast<uint8_t *>(mapped) + footprint.Offset + row * footprint.Footprint.RowPitch,
                    image.rgba.data() + row * image.width * 4, image.width * 4);
    upload->Unmap(0, nullptr);
    CheckHR(allocator_->Reset(), "Texture allocator Reset");
    CheckHR(commands_->Reset(allocator_.Get(), nullptr), "Texture command Reset");
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = upload.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = textures_[slot].Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    commands_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    auto barrier = Transition(textures_[slot].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commands_->ResourceBarrier(1, &barrier);
    CheckHR(commands_->Close(), "Texture upload Close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitForGpu();
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = description.Format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1;
    auto handle = textureHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += slot * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device_->CreateShaderResourceView(textures_[slot].Get(), &view, handle);
}

void Sample::CreatePipeline()
{
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 9;
    range.BaseShaderRegister = 0;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable = {1, &range};
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    const auto min = model_.sampler.minFilter;
    const auto minType =
        (min == 9728 || min == 9984 || min == 9986) ? D3D12_FILTER_TYPE_POINT : D3D12_FILTER_TYPE_LINEAR;
    const auto magType =
        model_.sampler.magFilter == 9728 ? D3D12_FILTER_TYPE_POINT : D3D12_FILTER_TYPE_LINEAR;
    const auto mipType = (min == 9986 || min == 9987) ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
    sampler.Filter =
        D3D12_ENCODE_BASIC_FILTER(minType, magType, mipType, D3D12_FILTER_REDUCTION_TYPE_STANDARD);
    auto address = [](uint32_t wrap) {
        return wrap == 33071   ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP
               : wrap == 33648 ? D3D12_TEXTURE_ADDRESS_MODE_MIRROR
                               : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    };
    sampler.AddressU = address(model_.sampler.wrapS);
    sampler.AddressV = address(model_.sampler.wrapT);
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = 0; // Only mip 0 is uploaded in this sample.
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root{};
    root.NumParameters = 2;
    root.pParameters = parameters;
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {sampler, sampler};
    samplers[1].ShaderRegister = 1;
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].MaxLOD = 8;
    samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    root.NumStaticSamplers = 2;
    root.pStaticSamplers = samplers;
    root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> serialized, error;
    const auto result = D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error);
    if (FAILED(result) && error)
        Trace(static_cast<const char *>(error->GetBufferPointer()));
    CheckHR(result, "D3D12SerializeRootSignature");
    CheckHR(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                         IID_PPV_ARGS(&rootSignature_)),
            "CreateRootSignature");
    const auto vertex = ReadBinary(AssetPath(L"Shaders/Vertex.cso"));
    const auto pixel = ReadBinary(AssetPath(L"Shaders/Pixel.cso"));
    const D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = rootSignature_.Get();
    pipeline.VS = {vertex.data(), vertex.size()};
    pipeline.PS = {pixel.data(), pixel.size()};
    pipeline.InputLayout = {layout, static_cast<UINT>(std::size(layout))};
    pipeline.SampleMask = UINT_MAX;
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.DepthStencilState.DepthEnable = TRUE;
    pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    pipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pipeline.SampleDesc.Count = 1;
    CheckHR(device_->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&pipeline_)),
            "CreateGraphicsPipelineState");
    const auto skyVertex = ReadBinary(AssetPath(L"Shaders/SkyVertex.cso")),
               skyPixel = ReadBinary(AssetPath(L"Shaders/SkyPixel.cso"));
    auto sky = pipeline;
    sky.VS = {skyVertex.data(), skyVertex.size()};
    sky.PS = {skyPixel.data(), skyPixel.size()};
    sky.InputLayout = {nullptr, 0};
    sky.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    sky.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    CheckHR(device_->CreateGraphicsPipelineState(&sky, IID_PPV_ARGS(&skyPipeline_)), "Sky PSO");
    if constexpr (LAB_TECHNIQUE == 13)
    {
        const auto shadowVertex = ReadBinary(AssetPath(L"Shaders/ShadowVertex.cso"));
        auto shadow = pipeline;
        shadow.VS = {shadowVertex.data(), shadowVertex.size()};
        shadow.PS = {nullptr, 0};
        shadow.NumRenderTargets = 0;
        shadow.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
        CheckHR(device_->CreateGraphicsPipelineState(&shadow, IID_PPV_ARGS(&shadowPipeline_)), "Shadow PSO");
    }
}
void Sample::CreateScene(const Options &)
{
    objects_.resize(scene_.objects.size());
    constants_ = UploadBuffer(nullptr, 512 * objects_.size());
    D3D12_RANGE empty{0, 0};
    CheckHR(constants_->Map(0, &empty, &constantsData_), "Lab constants Map");
    textures_.resize(objects_.size() * 9);
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = static_cast<UINT>(objects_.size() * 9 + 1);
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CheckHR(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&textureHeap_)), "Lab descriptors");
    for (size_t i = 0; i < objects_.size(); ++i)
    {
        const auto &m = scene_.objects[i].model;
        auto v = MakePbrVertices(m);
        auto &o = objects_[i];
        o.vertices = UploadBuffer(v.data(), v.size() * sizeof(PbrVertex));
        o.indices = UploadBuffer(m.mesh.indices.data(), m.mesh.indices.size() * 4);
        o.vertexView = {o.vertices->GetGPUVirtualAddress(), static_cast<UINT>(v.size() * sizeof(PbrVertex)),
                        sizeof(PbrVertex)};
        o.indexView = {o.indices->GetGPUVirtualAddress(), static_cast<UINT>(m.mesh.indices.size() * 4),
                       DXGI_FORMAT_R32_UINT};
        o.count = static_cast<UINT>(m.mesh.indices.size());
        LoadTexture(m.albedoPath, static_cast<uint32_t>(i * 9), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        LoadTexture(m.normalPath, static_cast<uint32_t>(i * 9 + 1), DXGI_FORMAT_R8G8B8A8_UNORM);
        LoadTexture(m.ormPath, static_cast<uint32_t>(i * 9 + 2), DXGI_FORMAT_R8G8B8A8_UNORM);
        LoadTexture(m.emissivePath, static_cast<uint32_t>(i * 9 + 3), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    }
    for (size_t e = 0; e < 2; ++e)
        for (size_t kind = 0; kind < 3; ++kind)
        {
            const wchar_t *suffix = kind == 0   ? L"_Environment.dds"
                                    : kind == 1 ? L"_Irradiance.dds"
                                                : L"_Specular.dds";
            environments_[e][kind] =
                LoadDdsTexture(std::filesystem::path(scene_.environments[e].wstring() + suffix));
        }
    environments_[2][0] = LoadDdsTexture(AssetPath(L"Assets/Environments/LightingLab/Orientation.dds"));
    environments_[2][1] = environments_[0][1];
    environments_[2][2] = environments_[0][2];
    brdf_ = LoadDdsTexture(AssetPath(L"Assets/Environments/LightingLab/BRDF.dds"));
    D3D12_RESOURCE_DESC shadow{};
    shadow.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    shadow.Width = shadow.Height = 2048;
    shadow.DepthOrArraySize = shadow.MipLevels = 1;
    shadow.Format = DXGI_FORMAT_R32_TYPELESS;
    shadow.SampleDesc.Count = 1;
    shadow.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_HEAP_PROPERTIES props{};
    props.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1;
    CheckHR(device_->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &shadow,
                                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
                                             IID_PPV_ARGS(&shadow_)),
            "Shadow texture");
    D3D12_DESCRIPTOR_HEAP_DESC dsv{};
    dsv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv.NumDescriptors = 1;
    CheckHR(device_->CreateDescriptorHeap(&dsv, IID_PPV_ARGS(&shadowHeap_)), "Shadow DSV heap");
    D3D12_DEPTH_STENCIL_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_D32_FLOAT;
    view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(shadow_.Get(), &view, shadowHeap_->GetCPUDescriptorHandleForHeapStart());
    UpdateLabDescriptors();
}
Sample::ComPtr<ID3D12Resource> Sample::LoadDdsTexture(const std::filesystem::path &path)
{
    const auto d = LoadDds(path);
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = d.width;
    desc.Height = d.height;
    desc.DepthOrArraySize = static_cast<UINT16>(d.faces);
    desc.MipLevels = static_cast<UINT16>(d.mips);
    desc.Format =
        d.format == DdsFormat::Rg16Float ? DXGI_FORMAT_R16G16_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{desc.Format};
    CheckHR(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)),
            "HDR format query");
    if (!(support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE))
        throw std::runtime_error("HDR sampling unsupported");
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> image;
    CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&image)),
            "HDR image");
    UINT n = static_cast<UINT>(d.subresources.size());
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(n);
    UINT64 bytes = 0;
    device_->GetCopyableFootprints(&desc, 0, n, 0, footprints.data(), nullptr, nullptr, &bytes);
    auto upload = UploadBuffer(nullptr, bytes);
    void *mapped = nullptr;
    D3D12_RANGE empty{0, 0};
    CheckHR(upload->Map(0, &empty, &mapped), "DDS upload Map");
    for (UINT i = 0; i < n; ++i)
        for (UINT row = 0; row < d.subresources[i].height; ++row)
            std::memcpy(static_cast<uint8_t *>(mapped) + footprints[i].Offset +
                            row * footprints[i].Footprint.RowPitch,
                        d.subresources[i].bytes.data() + row * d.subresources[i].rowPitch,
                        d.subresources[i].rowPitch);
    upload->Unmap(0, nullptr);
    CheckHR(allocator_->Reset(), "DDS allocator");
    CheckHR(commands_->Reset(allocator_.Get(), nullptr), "DDS commands");
    for (UINT i = 0; i < n; ++i)
    {
        D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
        src.pResource = upload.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprints[i];
        dst.pResource = image.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = i;
        commands_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    auto barrier =
        Transition(image.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commands_->ResourceBarrier(1, &barrier);
    CheckHR(commands_->Close(), "DDS close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitForGpu();
    return image;
}
void Sample::UpdateLabDescriptors()
{
    UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto start = textureHeap_->GetCPUDescriptorHandleForHeapStart();
    for (size_t i = 0; i < objects_.size(); ++i)
        for (size_t kind = 0; kind < 5; ++kind)
        {
            auto *resource = kind < 3    ? environments_[panel_.environmentIndex][kind].Get()
                             : kind == 3 ? brdf_.Get()
                                         : shadow_.Get();
            const auto desc = resource->GetDesc();
            D3D12_SHADER_RESOURCE_VIEW_DESC v{};
            v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            v.Format = kind == 4 ? DXGI_FORMAT_R32_FLOAT : desc.Format;
            if (kind < 3)
            {
                v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                v.TextureCube.MipLevels = desc.MipLevels;
            }
            else
            {
                v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                v.Texture2D.MipLevels = 1;
            }
            auto h = start;
            h.ptr += (i * 9 + 4 + kind) * stride;
            device_->CreateShaderResourceView(resource, &v, h);
        }
}
