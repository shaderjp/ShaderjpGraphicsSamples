#include "Sample.h"
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
    CheckHR(device_->CheckFeatureSupport1(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel), TUTORIAL_D3D12_SDK_VERSION),
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
void Sample::OnUpdate(float)
{
    const auto constants = MakeSceneConstants(width_, height_);
    std::memcpy(constantsData_, &constants, sizeof(constants));
}
void Sample::OnRender()
{
    const UINT index = swapchain_->GetCurrentBackBufferIndex();
    CheckHR(allocator_->Reset(), "Allocator Reset");
    CheckHR(commands_->Reset(allocator_.Get(), pipeline_.Get()), "Command list Reset");
    auto begin =
        Transition(targets_[index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands_->ResourceBarrier(1, &begin);
    auto target = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    target.ptr += index * rtvStride_;
    const float clear[4] = {0.025f, 0.035f, 0.06f, 1.0f};
    commands_->ClearRenderTargetView(target, clear, 0, nullptr);
    D3D12_VIEWPORT viewport{0, 0, float(width_), float(height_), 0, 1};
    D3D12_RECT scissor{0, 0, LONG(width_), LONG(height_)};
    commands_->RSSetViewports(1, &viewport);
    commands_->RSSetScissorRects(1, &scissor);
    commands_->SetGraphicsRootSignature(rootSignature_.Get());
    auto depthView = depthHeap_->GetCPUDescriptorHandleForHeapStart();
    commands_->ClearDepthStencilView(depthView, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commands_->OMSetRenderTargets(1, &target, FALSE, &depthView);
    commands_->SetGraphicsRootConstantBufferView(0, constants_->GetGPUVirtualAddress());
    ID3D12DescriptorHeap *heaps[] = {textureHeap_.Get()};
    commands_->SetDescriptorHeaps(1, heaps);
    commands_->SetGraphicsRootDescriptorTable(1, textureHeap_->GetGPUDescriptorHandleForHeapStart());
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands_->IASetVertexBuffers(0, 1, &vertexView_);
    commands_->IASetIndexBuffer(&indexView_);
    commands_->DrawIndexedInstanced(drawCount_, 1, 0, 0, 0);
    auto end =
        Transition(targets_[index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    commands_->ResourceBarrier(1, &end);
    CheckHR(commands_->Close(), "Close frame");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    CheckHR(swapchain_->Present(1, 0), "Present");
    // Deliberately serialize frames for this introductory series.
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
        texture_.Reset();
        textureHeap_.Reset();
        ReadValidationMessages();
    }
    catch (const std::exception &error)
    {
        ValidationError(error.what());
    }
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

void Sample::LoadTexture(const std::filesystem::path &path)
{
    const auto image = LoadWicImage(path);
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = image.width;
    description.Height = image.height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    description.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&texture_)),
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
    destination.pResource = texture_.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    commands_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    auto barrier = Transition(texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commands_->ResourceBarrier(1, &barrier);
    CheckHR(commands_->Close(), "Texture upload Close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitForGpu();
    D3D12_DESCRIPTOR_HEAP_DESC descriptors{};
    descriptors.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptors.NumDescriptors = 1;
    descriptors.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CheckHR(device_->CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(&textureHeap_)), "Create SRV heap");
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = description.Format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(texture_.Get(), &view,
                                      textureHeap_->GetCPUDescriptorHandleForHeapStart());
}

void Sample::CreatePipeline()
{
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable = {1, &range};
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root{};
    root.NumParameters = 2;
    root.pParameters = parameters;
    root.NumStaticSamplers = 1;
    root.pStaticSamplers = &sampler;
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
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = rootSignature_.Get();
    pipeline.VS = {vertex.data(), vertex.size()};
    pipeline.PS = {pixel.data(), pixel.size()};
    pipeline.InputLayout = {layout, 2};
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
}
void Sample::CreateScene(const Options &options)
{
    const auto mesh = LoadObj(options.model.empty() ? AssetPath(L"Assets/Models/Cube.obj") : options.model);
    Trace("OBJ vertices: " + std::to_string(mesh.vertices.size()) +
          ", indices: " + std::to_string(mesh.indices.size()));
    vertexBuffer_ = UploadBuffer(mesh.vertices.data(), mesh.vertices.size() * sizeof(ModelVertex));
    vertexView_ = {vertexBuffer_->GetGPUVirtualAddress(),
                   static_cast<UINT>(mesh.vertices.size() * sizeof(ModelVertex)),
                   static_cast<UINT>(sizeof(ModelVertex))};
    drawCount_ = static_cast<uint32_t>(mesh.indices.size());
    indexBuffer_ = UploadBuffer(mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
    indexView_ = {indexBuffer_->GetGPUVirtualAddress(),
                  static_cast<UINT>(mesh.indices.size() * sizeof(uint32_t)), DXGI_FORMAT_R32_UINT};
    // D3D12 constant buffer storage must be aligned to 256 bytes.
    constants_ = UploadBuffer(nullptr, 256);
    D3D12_RANGE read{0, 0};
    CheckHR(constants_->Map(0, &read, &constantsData_), "Constant buffer Map");
    const auto constants = MakeSceneConstants(width_, height_);
    std::memcpy(constantsData_, &constants, sizeof(constants));
    LoadTexture(options.texture.empty() ? AssetPath(L"Assets/Textures/UV.png") : options.texture);
}
