#include "Sample.h"
#include "../../Shared/PostProcessCapture.h"
#include <fstream>
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
    options_ = options;
    workshop_ = LoadWorkshop(options.scene.empty() ? AssetPath(L"Assets/Scenes/Workshop/Workshop.json")
                                                   : options.scene);
    panel_.Initialize(workshop_, options);
    timing_.api = "Direct3D12";
    timing_.mode = options.forward ? "Forward" : DEFERRED_COMPUTE ? "Compute" : "Pixel";
    timing_.scene = workshop_.scenes[panel_.scene].name;
    timing_.benchmark = options.benchmark;
    timing_.output = options.timings;
    if (options.benchmark && timing_.output.empty())
        timing_.output =
            AssetPath(std::filesystem::path(L"Benchmarks") / (timing_.scene + "-" + timing_.mode + ".json"));
#ifdef _DEBUG
    timing_.configuration = "Debug";
#else
    timing_.configuration = "Release";
#endif
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
        {
            timing_.gpu = std::filesystem::path(description.Description).string();
            break;
        }
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
    CreateScene();
    CreateSizeResources();
    CreatePipeline();
    CheckHR(queue_->GetTimestampFrequency(&timestampFrequency_), "Timestamp frequency");
    D3D12_QUERY_HEAP_DESC q{};
    q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    q.Count = 20;
    CheckHR(device_->CreateQueryHeap(&q, IID_PPV_ARGS(&queries_)), "Timestamp heap");
    D3D12_HEAP_PROPERTIES h{};
    h.Type = D3D12_HEAP_TYPE_READBACK;
    auto d = BufferDescription(20 * 8);
    CheckHR(device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr, IID_PPV_ARGS(&queryReadback_)),
            "Timestamp readback");

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
    const bool cameraHandled = panel_.navigation.Message(window, message, wParam, lParam);
    result = ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
    const auto &io = ImGui::GetIO();
    return cameraHandled || result != 0 ||
           (io.WantCaptureKeyboard &&
            (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN ||
             message == WM_SYSKEYUP || message == WM_CHAR));
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
    CheckHR(device_->CheckFeatureSupport1(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support), TUTORIAL_D3D12_SDK_VERSION),
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
        Transition(image.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    commands_->ResourceBarrier(1, &barrier);
    CheckHR(commands_->Close(), "DDS close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitForGpu();
    return image;
}

void Sample::Move(ID3D12Resource *image, D3D12_RESOURCE_STATES &state, D3D12_RESOURCE_STATES next)
{
    if (state == next)
        return;
    auto b = Transition(image, state, next);
    commands_->ResourceBarrier(1, &b);
    state = next;
}
void Sample::CreateScene()
{
    for (const auto &p : workshop_.primitives)
    {
        Geometry g;
        g.vertices = UploadBuffer(p.vertices.data(), p.vertices.size() * sizeof(PbrVertex));
        g.indices = UploadBuffer(p.indices.data(), p.indices.size() * 4);
        g.vb = {g.vertices->GetGPUVirtualAddress(), UINT(p.vertices.size() * sizeof(PbrVertex)),
                sizeof(PbrVertex)};
        g.ib = {g.indices->GetGPUVirtualAddress(), UINT(p.indices.size() * 4), DXGI_FORMAT_R32_UINT};
        g.count = UINT(p.indices.size());
        geometry_.push_back(g);
    }
    for (const auto &im : workshop_.images)
    {
        auto mips = GenerateMipChain(LoadWicImage(im.path), im.semantic, im.alphaCutoff);
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = mips[0].width;
        d.Height = mips[0].height;
        d.DepthOrArraySize = 1;
        d.MipLevels = UINT16(mips.size());
        d.SampleDesc.Count = 1;
        d.Format = im.semantic == ImageSemantic::Color ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                                       : DXGI_FORMAT_R8G8B8A8_UNORM;
        D3D12_HEAP_PROPERTIES h{};
        h.Type = D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> image;
        CheckHR(device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST,
                                                 nullptr, IID_PPV_ARGS(&image)),
                "PNG texture");
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> feet(mips.size());
        UINT64 bytes = 0;
        device_->GetCopyableFootprints(&d, 0, UINT(mips.size()), 0, feet.data(), nullptr, nullptr, &bytes);
        auto upload = UploadBuffer(nullptr, bytes);
        void *data = nullptr;
        D3D12_RANGE empty{};
        CheckHR(upload->Map(0, &empty, &data), "PNG map");
        for (size_t m = 0; m < mips.size(); ++m)
            for (UINT y = 0; y < mips[m].height; ++y)
                std::memcpy(static_cast<uint8_t *>(data) + feet[m].Offset + y * feet[m].Footprint.RowPitch,
                            mips[m].rgba.data() + size_t(y) * mips[m].width * 4, mips[m].width * 4);
        upload->Unmap(0, nullptr);
        CheckHR(allocator_->Reset(), "PNG allocator");
        CheckHR(commands_->Reset(allocator_.Get(), nullptr), "PNG commands");
        for (UINT m = 0; m < mips.size(); ++m)
        {
            D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
            src.pResource = upload.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = feet[m];
            dst.pResource = image.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = m;
            commands_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }
        auto barrier =
            Transition(image.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
        commands_->ResourceBarrier(1, &barrier);
        CheckHR(commands_->Close(), "PNG close");
        ID3D12CommandList *lists[] = {commands_.Get()};
        queue_->ExecuteCommandLists(1, lists);
        WaitForGpu();
        textures_.push_back(image);
    }
    const wchar_t *suffix[] = {L"_Environment.dds", L"_Irradiance.dds", L"_Specular.dds"};
    for (int i = 0; i < 3; ++i)
        environment_[i] = LoadDdsTexture(workshop_.environment.wstring() + suffix[i]);
    brdf_ = LoadDdsTexture(workshop_.environment.parent_path() / L"BRDF.dds");
    constants_ = UploadBuffer(nullptr, 1810 * 512);
    D3D12_RANGE empty{};
    CheckHR(constants_->Map(0, &empty, &constantsData_), "Scene constants");
    postConstants_ = UploadBuffer(nullptr, 4 * 256);
    CheckHR(postConstants_->Map(0, &empty, &postData_), "Post constants");
    points_ = UploadBuffer(nullptr, 72 * sizeof(WorkshopLight));
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = UINT(workshop_.materials.size()) * 16 + 1;
    fontSlot_ = heap.NumDescriptors - 1;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CheckHR(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&textureHeap_)), "Scene descriptors");
    heap.NumDescriptors = 12;
    CheckHR(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&postHeap_)), "Post descriptors");
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = 5;
    CheckHR(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&sceneRtv_)), "MRT heap");
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap.NumDescriptors = 3;
    CheckHR(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&sceneDsv_)), "Depth heap");
}
void Sample::CreateSizeResources()
{
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R16G16_SNORM,
                                   DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R11G11B10_FLOAT,
                                   DXGI_FORMAT_R32_TYPELESS};
    auto make = [&](UINT w, UINT h, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                    D3D12_RESOURCE_STATES state, bool depth)
    {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{depth ? DXGI_FORMAT_D32_FLOAT : format};
        CheckHR(device_->CheckFeatureSupport1(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support), TUTORIAL_D3D12_SDK_VERSION),
                "Render format query");
        if (!(support.Support1 &
              (depth ? D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL : D3D12_FORMAT_SUPPORT1_RENDER_TARGET)))
            throw std::runtime_error("Required render format unsupported");
        if (!depth && !(support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD))
            throw std::runtime_error("Required shader load format unsupported");
        if ((flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) &&
            !(support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))
            throw std::runtime_error("HDR UAV stores unsupported");
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w;
        d.Height = h;
        d.DepthOrArraySize = d.MipLevels = 1;
        d.Format = format;
        d.SampleDesc.Count = 1;
        d.Flags = flags;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = depth ? DXGI_FORMAT_D32_FLOAT : format;
        if (depth)
            clear.DepthStencil.Depth = 1;
        ComPtr<ID3D12Resource> image;
        CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, &clear,
                                                 IID_PPV_ARGS(&image)),
                "Deferred image");
        return image;
    };
    auto rt = sceneRtv_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 5; ++i)
    {
        gStates_[i] = i < 4 ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_DEPTH_WRITE;
        gbuffer_[i] =
            make(width_, height_, formats[i],
                 i < 4 ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                 gStates_[i], i == 4);
        if (i < 4)
        {
            device_->CreateRenderTargetView(gbuffer_[i].Get(), nullptr, rt);
            rt.ptr += rtvStride_;
        }
    }
    auto depth = sceneDsv_->GetCPUDescriptorHandleForHeapStart();
    UINT dsvStride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(gbuffer_[4].Get(), &dsv, depth);
    depth.ptr += dsvStride;
    dsv.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    device_->CreateDepthStencilView(gbuffer_[4].Get(), &dsv, depth);
    depth.ptr += dsvStride;
    dsv.Flags = D3D12_DSV_FLAG_NONE;
    if (!shadow_)
        shadow_ = make(2048, 2048, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                       D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
    device_->CreateDepthStencilView(shadow_.Get(), &dsv, depth);
    for (UINT i = 0; i < 4; ++i)
    {
        hdrStates_[i] = D3D12_RESOURCE_STATE_RENDER_TARGET;
        hdr_[i] = make(i ? std::max(1u, (width_ + 1) / 2) : width_,
                       i ? std::max(1u, (height_ + 1) / 2) : height_, DXGI_FORMAT_R16G16B16A16_FLOAT,
                       D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       hdrStates_[i], false);
    }
    device_->CreateRenderTargetView(hdr_[0].Get(), nullptr, rt);
    UpdateDescriptors();
}
void Sample::UpdateDescriptors()
{
    UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto srv =
        [&](ID3D12Resource *image, D3D12_CPU_DESCRIPTOR_HANDLE cpu, bool cube = false, bool depth = false)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
        v.Format = depth ? DXGI_FORMAT_R32_FLOAT : image->GetDesc().Format;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.ViewDimension = cube ? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
        if (cube)
            v.TextureCube.MipLevels = image->GetDesc().MipLevels;
        else
            v.Texture2D.MipLevels = image->GetDesc().MipLevels;
        device_->CreateShaderResourceView(image, &v, cpu);
    };
    for (UINT m = 0; m < workshop_.materials.size(); ++m)
    {
        auto cpu = textureHeap_->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += m * 16 * stride;
        for (UINT t = 0; t < 15; ++t)
        {
            if (t < 4)
                srv(textures_[workshop_.materials[m].images[t]].Get(), cpu);
            else if (t < 7)
                srv(environment_[t - 4].Get(), cpu, true);
            else if (t == 7)
                srv(brdf_.Get(), cpu);
            else if (t == 8)
                srv(shadow_.Get(), cpu, false, true);
            else if (t == 9)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC v{};
                v.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                v.Buffer.NumElements = 72;
                v.Buffer.StructureByteStride = sizeof(WorkshopLight);
                device_->CreateShaderResourceView(points_.Get(), &v, cpu);
            }
            else
                srv(gbuffer_[t - 10].Get(), cpu, false, t == 14);
            cpu.ptr += stride;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
        u.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device_->CreateUnorderedAccessView(hdr_[0].Get(), nullptr, &u, cpu);
    }
    for (UINT p = 0; p < 4; ++p)
    {
        auto cpu = postHeap_->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += p * 3 * stride;
        UINT input = p < 3 ? p : panel_.view >= 14 ? UINT(panel_.view - 13) : 0;
        srv(hdr_[input].Get(), cpu);
        cpu.ptr += stride;
        srv(hdr_[3].Get(), cpu);
        cpu.ptr += stride;
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
        u.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device_->CreateUnorderedAccessView(hdr_[p < 3 ? p + 1 : 0].Get(), nullptr, &u, cpu);
    }
}
void Sample::CreatePipeline()
{
    auto root = [&](ComPtr<ID3D12RootSignature> &out, UINT resources, UINT samplers)
    {
        D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, resources, 0, 0, 0},
                                            {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0}};
        D3D12_ROOT_PARAMETER p[3]{};
        p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        p[0].Descriptor.ShaderRegister = 0;
        for (UINT i = 0; i < 2; ++i)
        {
            p[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p[i + 1].DescriptorTable = {1, &ranges[i]};
        }
        D3D12_STATIC_SAMPLER_DESC s[2]{};
        for (UINT i = 0; i < 2; ++i)
        {
            s[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            s[i].AddressU = s[i].AddressV = s[i].AddressW =
                i || resources == 2 ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            s[i].ShaderRegister = i;
            s[i].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            s[i].MaxLOD = D3D12_FLOAT32_MAX;
        }
        D3D12_ROOT_SIGNATURE_DESC d{3, p, samplers, s,
                                    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        ComPtr<ID3DBlob> b, e;
        CheckHR(D3D12SerializeRootSignature(&d, D3D_ROOT_SIGNATURE_VERSION_1, &b, &e), "Root serialize");
        CheckHR(
            device_->CreateRootSignature(0, b->GetBufferPointer(), b->GetBufferSize(), IID_PPV_ARGS(&out)),
            "Root signature");
    };
    root(rootSignature_, 15, 2);
    root(postRoot_, 2, 1);
    const D3D12_INPUT_ELEMENT_DESC a[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    auto graphics = [&](ComPtr<ID3D12PipelineState> &out, const wchar_t *vertex, const wchar_t *pixel,
                        int kind, bool twoSided)
    {
        auto v = ReadBinary(AssetPath(std::filesystem::path(L"Shaders") / (std::wstring(vertex) + L".cso"))),
             p = ReadBinary(AssetPath(std::filesystem::path(L"Shaders") / (std::wstring(pixel) + L".cso")));
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
        d.pRootSignature = (kind == 5 || kind == 7) ? postRoot_.Get() : rootSignature_.Get();
        d.VS = {v.data(), v.size()};
        d.PS = {p.data(), p.size()};
        d.SampleMask = UINT_MAX;
        d.SampleDesc.Count = 1;
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = twoSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
        d.RasterizerState.DepthClipEnable = TRUE;
        d.DepthStencilState.DepthFunc = kind == 3 ? D3D12_COMPARISON_FUNC_EQUAL : D3D12_COMPARISON_FUNC_LESS;
        d.DepthStencilState.DepthEnable = kind <= 3 || kind == 6;
        d.DepthStencilState.DepthWriteMask =
            kind <= 2 ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        d.DSVFormat = d.DepthStencilState.DepthEnable ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN;
        if (kind <= 2 || kind == 6)
            d.InputLayout = {a, 4};
        d.NumRenderTargets = kind == 0 ? 4 : kind == 2 ? 0 : 1;
        for (UINT i = 0; i < 4; ++i)
            d.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        if (kind == 0)
        {
            d.BlendState.IndependentBlendEnable = TRUE;
            d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            d.RTVFormats[1] = DXGI_FORMAT_R16G16_SNORM;
            d.RTVFormats[2] = DXGI_FORMAT_R8G8_UNORM;
            d.RTVFormats[3] = DXGI_FORMAT_R11G11B10_FLOAT;
        }
        else if (kind != 2)
            d.RTVFormats[0] = kind == 5 ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (kind == 6)
        {
            auto &b = d.BlendState.RenderTarget[0];
            b.BlendEnable = TRUE;
            b.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            b.BlendOp = b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            b.SrcBlendAlpha = D3D12_BLEND_ONE;
            b.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        }
        CheckHR(device_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&out)), "Deferred graphics pipeline");
    };
    for (UINT i = 0; i < 2; ++i)
    {
        graphics(gPipelines_[i], L"GBufferVertex", L"GBufferPixel", 0, i != 0);
        graphics(forwardPipelines_[i], L"GBufferVertex", L"ForwardPixel", 1, i != 0);
        graphics(shadowPipelines_[i], L"ShadowVertex", L"ShadowPixel", 2, i != 0);
        graphics(transparentPipelines_[i], L"GBufferVertex", L"ForwardPixel", 6, i != 0);
    }
    graphics(skyPipeline_, L"SkyVertex", L"SkyPixel", 3, true);
    graphics(lightingPixel_, L"PostVertex", L"LightingPixel", 4, true);
    graphics(postPipeline_, L"PostVertex", L"DeferredPostPixel", 5, true);
    graphics(testPipeline_, L"PostVertex", L"TestPixel", 7, true);
    auto compute = [&](ComPtr<ID3D12PipelineState> &out, const wchar_t *name, ID3D12RootSignature *root)
    {
        auto bytes =
            ReadBinary(AssetPath(std::filesystem::path(L"Shaders") / (std::wstring(name) + L".cso")));
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
        d.pRootSignature = root;
        d.CS = {bytes.data(), bytes.size()};
        CheckHR(device_->CreateComputePipelineState(&d, IID_PPV_ARGS(&out)), "Deferred compute pipeline");
    };
    compute(lightingCompute_, L"LightingCompute", rootSignature_.Get());
    compute(bloomPipelines_[0], L"Extract", postRoot_.Get());
    compute(bloomPipelines_[1], L"Blur", postRoot_.Get());
}
void Sample::Bind(UINT object, UINT material, bool compute)
{
    ID3D12DescriptorHeap *heaps[] = {textureHeap_.Get()};
    commands_->SetDescriptorHeaps(1, heaps);
    UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto srv = textureHeap_->GetGPUDescriptorHandleForHeapStart();
    srv.ptr += material * 16 * stride;
    auto uav = srv;
    uav.ptr += 15 * stride;
    if (compute)
    {
        commands_->SetComputeRootSignature(rootSignature_.Get());
        commands_->SetComputeRootConstantBufferView(0, constants_->GetGPUVirtualAddress() + object * 512);
        commands_->SetComputeRootDescriptorTable(1, srv);
        commands_->SetComputeRootDescriptorTable(2, uav);
    }
    else
    {
        commands_->SetGraphicsRootSignature(rootSignature_.Get());
        commands_->SetGraphicsRootConstantBufferView(0, constants_->GetGPUVirtualAddress() + object * 512);
        commands_->SetGraphicsRootDescriptorTable(1, srv);
    }
}
void Sample::DrawObjects(int pass)
{
    const auto &s = workshop_.scenes[panel_.scene];
    std::vector<UINT> order;
    for (UINT i = 0; i < s.instances.size(); ++i)
    {
        const auto &m = workshop_.materials[workshop_.primitives[s.instances[i].primitive].material];
        if (panel_.Includes(m.alpha) && ((pass == 3) == (m.alpha == AlphaMode::Blend)))
            order.push_back(i);
    }
    if (pass == 3)
    {
        auto c = *reinterpret_cast<const DeferredConstants *>(constantsData_);
        auto distance = [&](UINT i)
        {
            auto &o = s.instances[i];
            float x = (o.minimum.x + o.maximum.x) * .5f - c.camera.x,
                  y = (o.minimum.y + o.maximum.y) * .5f - c.camera.y,
                  z = (o.minimum.z + o.maximum.z) * .5f - c.camera.z;
            return x * x + y * y + z * z;
        };
        std::stable_sort(order.begin(), order.end(),
                         [&](UINT a, UINT b) { return distance(a) > distance(b); });
    }
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for (auto i : order)
    {
        const auto &instance = s.instances[i];
        const auto &p = workshop_.primitives[instance.primitive];
        const auto &m = workshop_.materials[p.material];
        auto &g = geometry_[instance.primitive];
        commands_->SetPipelineState((pass == 0   ? shadowPipelines_
                                     : pass == 1 ? gPipelines_
                                     : pass == 2 ? forwardPipelines_
                                                 : transparentPipelines_)[m.doubleSided ? 1 : 0]
                                        .Get());
        Bind(i + 1, p.material);
        commands_->IASetVertexBuffers(0, 1, &g.vb);
        commands_->IASetIndexBuffer(&g.ib);
        commands_->DrawIndexedInstanced(g.count, 1, 0, 0, 0);
    }
}
void Sample::SetPost(UINT pass, bool compute)
{
    ID3D12DescriptorHeap *heaps[] = {postHeap_.Get()};
    commands_->SetDescriptorHeaps(1, heaps);
    auto srv = postHeap_->GetGPUDescriptorHandleForHeapStart();
    UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    srv.ptr += pass * 3 * stride;
    auto uav = srv;
    uav.ptr += 2 * stride;
    if (compute)
    {
        commands_->SetComputeRootSignature(postRoot_.Get());
        commands_->SetComputeRootConstantBufferView(0, postConstants_->GetGPUVirtualAddress() + pass * 256);
        commands_->SetComputeRootDescriptorTable(1, srv);
        commands_->SetComputeRootDescriptorTable(2, uav);
    }
    else
    {
        commands_->SetGraphicsRootSignature(postRoot_.Get());
        commands_->SetGraphicsRootConstantBufferView(0, postConstants_->GetGPUVirtualAddress() + pass * 256);
        commands_->SetGraphicsRootDescriptorTable(1, srv);
    }
}
void Sample::Stamp(UINT index)
{
    commands_->EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
}
void Sample::OnUpdate(float seconds)
{
    if (auto path = panel_.TestCapture(); !path.empty())
        OnScreenshot(path);
    ImGui_ImplWin32_NewFrame();
    panel_.QueueTestInput();
    ImGui::NewFrame();
    panel_.Draw(workshop_, timing_, "Direct3D12");
    const auto &baseCamera =
        panel_.pathEnabled ? workshop_.path.at(panel_.pathFrame) : workshop_.cameras.at(panel_.camera);
    if (panel_.navigation.Update(window_, seconds, baseCamera,
                                 options_.benchmark))
        panel_.pathEnabled = panel_.pathPlaying = false;
    ImGui::Render();
    if (options_.benchmark)
        panel_.pathFrame = int(timing_.frame < 60 ? 0 : std::min(timing_.frame - 60, 1199u));
    else if (panel_.pathEnabled && panel_.pathPlaying)
        panel_.pathFrame = (panel_.pathFrame + 1) % 1201;
}
void Sample::OnRender()
{
    WaitForGpu();
    UpdateDescriptors();
    const auto &s = workshop_.scenes[panel_.scene];
    auto fill = [&](const WorkshopInstance *instance)
    {
        return MakeDeferredConstants(
            workshop_, panel_.scene, panel_.camera, panel_.pathEnabled ? panel_.pathFrame : -1, width_,
            height_, instance, UINT(std::min(panel_.view, 13)), panel_.direct, panel_.ibl, panel_.shadows,
            panel_.pcf, panel_.bias, panel_.exposure, panel_.intensity, panel_.yaw, panel_.occluder,
            panel_.navigation.Position(), panel_.navigation.Target(), panel_.navigation.Projection());
    };
    auto global = fill(nullptr);
    std::memcpy(constantsData_, &global, sizeof(global));
    for (UINT i = 0; i < s.instances.size(); ++i)
    {
        auto c = fill(&s.instances[i]);
        std::memcpy(static_cast<uint8_t *>(constantsData_) + (i + 1) * 512, &c, sizeof(c));
    }
    void *lightData = nullptr;
    D3D12_RANGE empty{};
    CheckHR(points_->Map(0, &empty, &lightData), "Light upload");
    std::memcpy(lightData, s.lights.data(), s.lights.size() * sizeof(WorkshopLight));
    points_->Unmap(0, nullptr);
    for (UINT p = 0; p < 4; ++p)
    {
        PostConstants c{};
        c.settings = {panel_.exposure, panel_.threshold, panel_.strength, float(panel_.view >= 14 ? 2 : 0)};
        c.dimensions = {width_, height_, std::max(1u, (width_ + 1) / 2), std::max(1u, (height_ + 1) / 2)};
        c.flags = {UINT(panel_.bloom && panel_.view == 0), p == 1 ? 1u : 0u,
                   options_.testImage == L"black"      ? 1u
                   : options_.testImage == L"constant" ? 2u
                   : options_.testImage == L"impulse"  ? 3u
                                                       : 0u,
                   UINT(panel_.view >= 1 && panel_.view <= 9 && panel_.view != 7)};
        std::memcpy(static_cast<uint8_t *>(postData_) + p * 256, &c, sizeof(c));
    }
    CheckHR(allocator_->Reset(), "Frame allocator");
    CheckHR(commands_->Reset(allocator_.Get(), nullptr), "Frame commands");
    Stamp(18);
    Stamp(0);
    auto dsv = sceneDsv_->GetCPUDescriptorHandleForHeapStart();
    UINT depthStride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    auto shadowDsv = dsv;
    shadowDsv.ptr += 2 * depthStride;
    D3D12_VIEWPORT viewport{0, 0, 2048, 2048, 0, 1};
    D3D12_RECT scissor{0, 0, 2048, 2048};
    commands_->RSSetViewports(1, &viewport);
    commands_->RSSetScissorRects(1, &scissor);
    Move(shadow_.Get(), shadowState_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    commands_->ClearDepthStencilView(shadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
    commands_->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsv);
    if (panel_.shadows)
        DrawObjects(0);
    Move(shadow_.Get(), shadowState_, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    Stamp(1);
    viewport = {0, 0, float(width_), float(height_), 0, 1};
    scissor = {0, 0, LONG(width_), LONG(height_)};
    commands_->RSSetViewports(1, &viewport);
    commands_->RSSetScissorRects(1, &scissor);
    Stamp(2);
    Move(gbuffer_[4].Get(), gStates_[4], D3D12_RESOURCE_STATE_DEPTH_WRITE);
    commands_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
    auto hdrRtv = sceneRtv_->GetCPUDescriptorHandleForHeapStart();
    hdrRtv.ptr += 4 * rtvStride_;
    float clear[4]{};
    if (panel_.forward)
    {
        Move(hdr_[0].Get(), hdrStates_[0], D3D12_RESOURCE_STATE_RENDER_TARGET);
        commands_->ClearRenderTargetView(hdrRtv, clear, 0, nullptr);
        commands_->OMSetRenderTargets(1, &hdrRtv, FALSE, &dsv);
        DrawObjects(2);
    }
    else
    {
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 4> rt;
        for (UINT i = 0; i < 4; ++i)
        {
            Move(gbuffer_[i].Get(), gStates_[i], D3D12_RESOURCE_STATE_RENDER_TARGET);
            rt[i] = sceneRtv_->GetCPUDescriptorHandleForHeapStart();
            rt[i].ptr += i * rtvStride_;
            commands_->ClearRenderTargetView(rt[i], clear, 0, nullptr);
        }
        commands_->OMSetRenderTargets(4, rt.data(), FALSE, &dsv);
        DrawObjects(1);
        for (UINT i = 0; i < 4; ++i)
            Move(gbuffer_[i].Get(), gStates_[i], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    }
    Move(gbuffer_[4].Get(), gStates_[4],
         D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    Stamp(3);
    Stamp(4);
    if (!panel_.forward)
    {
        if constexpr (DEFERRED_COMPUTE)
        {
            Move(hdr_[0].Get(), hdrStates_[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commands_->SetPipelineState(lightingCompute_.Get());
            Bind(0, 0, true);
            commands_->Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
        }
        else
        {
            Move(hdr_[0].Get(), hdrStates_[0], D3D12_RESOURCE_STATE_RENDER_TARGET);
            commands_->OMSetRenderTargets(1, &hdrRtv, FALSE, nullptr);
            commands_->SetPipelineState(lightingPixel_.Get());
            Bind(0, 0);
            commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commands_->DrawInstanced(3, 1, 0, 0);
        }
    }
    Stamp(5);
    Stamp(6);
    Move(hdr_[0].Get(), hdrStates_[0], D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto readDsv = dsv;
    readDsv.ptr += depthStride;
    commands_->OMSetRenderTargets(1, &hdrRtv, FALSE, &readDsv);
    commands_->SetPipelineState(skyPipeline_.Get());
    Bind(0, 0);
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands_->DrawInstanced(3, 1, 0, 0);
    Stamp(7);
    Stamp(8);
    DrawObjects(3);
    if (!options_.testImage.empty())
    {
        commands_->OMSetRenderTargets(1, &hdrRtv, FALSE, nullptr);
        commands_->SetPipelineState(testPipeline_.Get());
        SetPost(3, false);
        commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands_->DrawInstanced(3, 1, 0, 0);
    }
    Stamp(9);
    Move(hdr_[0].Get(), hdrStates_[0], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    for (UINT p = 0; p < 3; ++p)
    {
        Stamp(10 + p * 2);
        Move(hdr_[p + 1].Get(), hdrStates_[p + 1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commands_->SetPipelineState(bloomPipelines_[p == 0 ? 0 : 1].Get());
        SetPost(p, true);
        commands_->Dispatch((std::max(1u, (width_ + 1) / 2) + 7) / 8,
                            (std::max(1u, (height_ + 1) / 2) + 7) / 8, 1);
        Move(hdr_[p + 1].Get(), hdrStates_[p + 1], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
        Stamp(11 + p * 2);
    }
    Stamp(16);
    renderedBuffer_ = swapchain_->GetCurrentBackBufferIndex();
    auto before = Transition(targets_[renderedBuffer_].Get(), D3D12_RESOURCE_STATE_PRESENT,
                             D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands_->ResourceBarrier(1, &before);
    auto target = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    target.ptr += renderedBuffer_ * rtvStride_;
    commands_->OMSetRenderTargets(1, &target, FALSE, nullptr);
    commands_->SetPipelineState(postPipeline_.Get());
    SetPost(3, false);
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands_->DrawInstanced(3, 1, 0, 0);
    RenderImGui();
    Stamp(17);
    Stamp(19);
    auto after = Transition(targets_[renderedBuffer_].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                            D3D12_RESOURCE_STATE_PRESENT);
    commands_->ResourceBarrier(1, &after);
    commands_->ResolveQueryData(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 20, queryReadback_.Get(), 0);
    CheckHR(commands_->Close(), "Frame close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    CheckHR(swapchain_->Present(options_.benchmark ? 0 : 1, 0), "Present");
    WaitForGpu();
    void *data = nullptr;
    D3D12_RANGE range{0, 160};
    CheckHR(queryReadback_->Map(0, &range, &data), "Query map");
    const auto *ticks = static_cast<const uint64_t *>(data);
    std::array<double, 10> values{};
    for (UINT i = 0; i < 10; ++i)
        values[i] = double(ticks[i * 2 + 1] - ticks[i * 2]) * 1000 / timestampFrequency_;
    queryReadback_->Unmap(0, nullptr);
    timing_.Record(values);
    ReadValidationMessages();
}
void Sample::OnResize(uint32_t width, uint32_t height)
{
    if (!width || !height)
        return;
    WaitForGpu();
    for (auto &target : targets_)
        target.Reset();
    for (auto &g : gbuffer_)
        g.Reset();
    for (auto &h : hdr_)
        h.Reset();
    width_ = width;
    height_ = height;
    CheckHR(swapchain_->ResizeBuffers(2, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0), "Resize buffers");
    CreateTargets();
    CreateSizeResources();
    ReadValidationMessages();
}
void Sample::DumpGpuData(const std::filesystem::path &path)
{
    auto directory = path.parent_path() / (path.stem().string() + "-gpu");
    std::filesystem::create_directories(directory);
    nlohmann::json metadata = {{"width", width_},
                               {"height", height_},
                               {"bytesPerPixel", 18},
                               {"scene", workshop_.scenes[panel_.scene].name},
                               {"forward", panel_.forward}};
    const char *names[] = {"base-ao", "normal",  "metal-rough", "emissive", "depth",
                           "hdr",     "extract", "horizontal",  "bloom",    "shadow-depth"};
    const UINT sizes[] = {4, 4, 2, 4, 4, 8, 8, 8, 8, 4};
    for (UINT i = 0; i < 10; ++i)
    {
        if (panel_.forward && i < 4)
            continue;
        auto image = i == 9 ? shadow_.Get() : i < 5 ? gbuffer_[i].Get() : hdr_[i - 5].Get();
        auto &state = i == 9 ? shadowState_ : i < 5 ? gStates_[i] : hdrStates_[i - 5];
        auto previous = state;
        auto d = image->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT f{};
        UINT64 bytes = 0;
        device_->GetCopyableFootprints(&d, 0, 1, 0, &f, nullptr, nullptr, &bytes);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        auto buffer = BufferDescription(bytes);
        ComPtr<ID3D12Resource> readback;
        CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&readback)),
                "G-buffer readback");
        CheckHR(allocator_->Reset(), "Readback allocator");
        CheckHR(commands_->Reset(allocator_.Get(), nullptr), "Readback commands");
        Move(image, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
        src.pResource = image;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = f;
        commands_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        Move(image, state, previous);
        CheckHR(commands_->Close(), "Readback close");
        ID3D12CommandList *lists[] = {commands_.Get()};
        queue_->ExecuteCommandLists(1, lists);
        WaitForGpu();
        void *data = nullptr;
        D3D12_RANGE range{0, SIZE_T(bytes)};
        CheckHR(readback->Map(0, &range, &data), "Readback map");
        std::vector<uint8_t> packed(size_t(d.Width) * d.Height * sizes[i]);
        for (UINT y = 0; y < d.Height; ++y)
            std::memcpy(packed.data() + size_t(y) * d.Width * sizes[i],
                        static_cast<uint8_t *>(data) + f.Offset + y * f.Footprint.RowPitch,
                        size_t(d.Width) * sizes[i]);
        readback->Unmap(0, nullptr);
        CaptureBytes(directory / (std::string(names[i]) + ".bin"), packed.data(), packed.size());
        if (i >= 5 && i < 9)
            metadata["images"].push_back(
                CaptureHalfImage(directory, names[i], UINT(d.Width), d.Height, packed));
    }
    CaptureMetadata(directory, metadata);
}
void Sample::OnDestroy() noexcept
{
    panel_.navigation.Reset();
    try
    {
        if (queue_ && fence_ && fenceEvent_)
            WaitForGpu();
        timing_.Save();
        DestroyImGui();
        ReadValidationMessages();
    }
    catch (const std::exception &e)
    {
        ValidationError(e.what());
    }
    if (constantsData_ && constants_)
        constants_->Unmap(0, nullptr);
    constantsData_ = nullptr;
    if (postData_ && postConstants_)
        postConstants_->Unmap(0, nullptr);
    postData_ = nullptr;
    DestroyImGui();
    geometry_.clear();
    textures_.clear();
    for (auto &g : gbuffer_)
        g.Reset();
    for (auto &h : hdr_)
        h.Reset();
    for (auto &e : environment_)
        e.Reset();
    for (auto &t : targets_)
        t.Reset();
    shadow_.Reset();
    brdf_.Reset();
    points_.Reset();
    constants_.Reset();
    postConstants_.Reset();
    queries_.Reset();
    queryReadback_.Reset();
    textureHeap_.Reset();
    postHeap_.Reset();
    sceneRtv_.Reset();
    sceneDsv_.Reset();
    for (auto *group : {&gPipelines_, &forwardPipelines_, &transparentPipelines_, &shadowPipelines_})
        for (auto &p : *group)
            p.Reset();
    for (auto &p : bloomPipelines_)
        p.Reset();
    lightingPixel_.Reset();
    lightingCompute_.Reset();
    skyPipeline_.Reset();
    postPipeline_.Reset();
    testPipeline_.Reset();
    rootSignature_.Reset();
    postRoot_.Reset();
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
        DumpGpuData(path);
}
