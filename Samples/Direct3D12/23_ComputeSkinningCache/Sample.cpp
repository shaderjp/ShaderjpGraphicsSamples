#include "Sample.h"
#include "../../Shared/MeshFixtures.h"
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
#pragma warning(push)
#pragma warning(disable : 4324) // D3D12 pipeline stream subobjects require pointer alignment.
#pragma warning(push)
#pragma warning(disable : 4324) // D3D12 pipeline stream subobjects require pointer alignment.
template <class T, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type> struct alignas(void *) MeshStreamObject
{
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
    T value{};
};
#pragma warning(pop)
#pragma warning(pop)

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
    workshop_ = skinning_.Build(options.model.empty()
                                    ? AssetPath(L"Assets/Models/SilverRobotAnimated/SilverRobot.gltf")
                                    : options.model);
    panel_.Initialize(workshop_, options, skinning_.model);
    workshop_.path.assign(1200, workshop_.cameras[panel_.camera]);
    timing_.api = "Direct3D12";
    timing_.mode = options.forward         ? "Forward"
                   : panel_.grid.mode == 0 ? "All"
                   : panel_.grid.mode == 1 ? "Tiled"
                                           : "Clustered";
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
    if constexpr (SKIN_TECHNIQUE == 3)
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 meshSupport{};
        CheckHR(device_->CheckFeatureSupport1(D3D12_FEATURE_D3D12_OPTIONS7, &meshSupport, sizeof(meshSupport), TUTORIAL_D3D12_SDK_VERSION),
                "Mesh Shader Tier query");
        Trace("Mesh Shader Tier: " + std::to_string(meshSupport.MeshShaderTier));
        if (meshSupport.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
            throw std::runtime_error("Mesh Shader Tier 1 is required");
    }
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
    if constexpr (SKIN_TECHNIQUE == 3)
        CheckHR(commands_.As(&meshCommands_), "Mesh command list");
    CheckHR(commands_->Close(), "Close initial command list");
    CheckHR(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_)
        throw std::runtime_error("CreateEventW failed");
    CreateTargets();
    CreateScene();
    CreateSizeResources();
    CreateMeshPipeline();
    CreatePipeline();
    CheckHR(queue_->GetTimestampFrequency(&timestampFrequency_), "Timestamp frequency");
    D3D12_QUERY_HEAP_DESC q{};
    q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    q.Count = 26;
    CheckHR(device_->CreateQueryHeap(&q, IID_PPV_ARGS(&queries_)), "Timestamp heap");
    D3D12_HEAP_PROPERTIES h{};
    h.Type = D3D12_HEAP_TYPE_READBACK;
    auto d = BufferDescription(26 * 8);
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
    CreateMeshResources();
    for (const auto &im : workshop_.images)
    {
        auto mips = GenerateMipChain(MeshTextureImage(im), im.semantic, im.alphaCutoff);
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
    points_ = UploadBuffer(nullptr, MaximumPointLights * sizeof(WorkshopLight));
    gridConstants_ = UploadBuffer(nullptr, 256);
    CheckHR(gridConstants_->Map(0, &empty, &gridConstantsData_), "Grid constants");
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    meshSlot_ = UINT(workshop_.materials.size()) * 22;
    heap.NumDescriptors = meshSlot_ + 11 + 64 + 2 + 4 + 1;
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
    CreateLightGridResources();
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
        cpu.ptr += m * 22 * stride;
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
                v.Buffer.NumElements = MaximumPointLights;
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
        cpu.ptr += stride;
        for (UINT i = 0; i < 2; ++i)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC v{};
            v.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            v.Buffer.NumElements = i == 0 ? gridCells_ : gridCells_ * LightGridStride;
            v.Buffer.StructureByteStride = i == 0 ? 8 : 4;
            device_->CreateShaderResourceView(gridBuffers_[i].Get(), &v, cpu);
            cpu.ptr += stride;
        }
        for (UINT i = 0; i < 4; ++i)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC v{};
            v.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            v.Buffer.StructureByteStride = i == 0 || i == 2 ? 8 : 4;
            v.Buffer.NumElements = UINT(gridBuffers_[i]->GetDesc().Width / v.Buffer.StructureByteStride);
            device_->CreateUnorderedAccessView(gridBuffers_[i].Get(), nullptr, &v, cpu);
            cpu.ptr += stride;
        }
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
    UpdateMeshDescriptors();
}
void Sample::CreatePipeline()
{
    auto root = [&](ComPtr<ID3D12RootSignature> &out, UINT resources, UINT samplers)
    {
        D3D12_DESCRIPTOR_RANGE ranges[4] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, resources, 0, 0, 0},
                                            {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0},
                                            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 15, 0, 0},
                                            {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 1, 0, 0}};
        D3D12_ROOT_PARAMETER p[6]{};
        p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        p[0].Descriptor.ShaderRegister = 0;
        for (UINT i = 0; i < 2; ++i)
        {
            p[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p[i + 1].DescriptorTable = {1, &ranges[i]};
        }
        p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        p[3].Descriptor.ShaderRegister = 1;
        p[4].ParameterType = p[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p[4].DescriptorTable = {1, &ranges[2]};
        p[5].DescriptorTable = {1, &ranges[3]};
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
        D3D12_ROOT_SIGNATURE_DESC d{resources == 15 ? 6u : 3u, p, samplers, s,
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
        d.pRootSignature = (kind == 5 || kind == 7) ? postRoot_.Get()
                           : kind <= 2              ? meshRoot_.Get()
                                                    : rootSignature_.Get();
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
        D3D12_INPUT_ELEMENT_DESC cacheInput[] = {{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
                                                  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                                                 {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
                                                  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                                                 {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,
                                                  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
        if (std::wstring(vertex) == L"ReferenceVertex")
            d.InputLayout = {};
        if (std::wstring(vertex) == L"CacheVertex")
            d.InputLayout = {cacheInput, 3};
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
        graphics(gPipelines_[i], L"CacheVertex", L"MeshGBufferPixel", 0, i != 0);
        graphics(forwardPipelines_[i], L"CacheVertex", L"MeshForwardPixel", 1, i != 0);
        graphics(shadowPipelines_[i], L"CacheVertex", L"MeshShadowPixel", 2, i != 0);
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
    compute(tileDepthPipeline_, L"TileDepth", rootSignature_.Get());
    compute(cullPipeline_, L"CullLights", rootSignature_.Get());
    compute(bloomPipelines_[0], L"Extract", postRoot_.Get());
    compute(bloomPipelines_[1], L"Blur", postRoot_.Get());
}
void Sample::Bind(UINT object, UINT material, bool compute)
{
    ID3D12DescriptorHeap *heaps[] = {textureHeap_.Get()};
    commands_->SetDescriptorHeaps(1, heaps);
    UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto srv = textureHeap_->GetGPUDescriptorHandleForHeapStart();
    srv.ptr += material * 22 * stride;
    auto uav = srv;
    uav.ptr += 15 * stride;
    if (compute)
    {
        commands_->SetComputeRootSignature(rootSignature_.Get());
        commands_->SetComputeRootConstantBufferView(0, constants_->GetGPUVirtualAddress() + object * 512);
        commands_->SetComputeRootDescriptorTable(1, srv);
        commands_->SetComputeRootDescriptorTable(2, uav);
        auto lists = srv;
        lists.ptr += 16 * stride;
        auto writable = srv;
        writable.ptr += 18 * stride;
        commands_->SetComputeRootConstantBufferView(3, gridConstants_->GetGPUVirtualAddress());
        commands_->SetComputeRootDescriptorTable(4, lists);
        commands_->SetComputeRootDescriptorTable(5, writable);
    }
    else
    {
        commands_->SetGraphicsRootSignature(rootSignature_.Get());
        commands_->SetGraphicsRootConstantBufferView(0, constants_->GetGPUVirtualAddress() + object * 512);
        commands_->SetGraphicsRootDescriptorTable(1, srv);
        auto lists = srv;
        lists.ptr += 16 * stride;
        commands_->SetGraphicsRootConstantBufferView(3, gridConstants_->GetGPUVirtualAddress());
        commands_->SetGraphicsRootDescriptorTable(4, lists);
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
    float animationClock = options_.benchmark        ? float(timing_.frame + 1) / 60.f
                           : options_.fixedDelta > 0 ? float(timing_.frame + 1) * options_.fixedDelta
                                                     : seconds;
    panel_.Advance(animationClock, skinning_.model);
    if (auto path = panel_.TestCapture(); !path.empty())
        OnScreenshot(path);
    ImGui_ImplWin32_NewFrame();
    float delta =
        options_.fixedDelta > 0 ? options_.fixedDelta : std::clamp(seconds - previousSeconds_, 0.f, .05f);
    previousSeconds_ = seconds;
    if (panel_.grid.animate)
        panel_.grid.phase += delta;
    panel_.QueueTestInput();

    ImGui::NewFrame();
    panel_.DrawSkin(skinning_.model, skinning_);
    const auto &baseCamera =
        panel_.pathEnabled ? workshop_.path.at(panel_.pathFrame) : workshop_.cameras.at(panel_.camera);
    if (panel_.navigation.Update(window_, seconds, baseCamera, options_.benchmark))
        panel_.pathEnabled = panel_.pathPlaying = false;
    ImGui::Render();
    if (options_.benchmark)
        panel_.pathFrame = int(timing_.frame < 60 ? 0 : std::min(timing_.frame - 60, 1199u));
    else if (panel_.pathEnabled && panel_.pathPlaying)
        panel_.pathFrame = (panel_.pathFrame + 1) % 1201;
}
void Sample::OnRender()
{
    skinning_.UpdateRootMotion(workshop_, size_t(panel_.animation),
                               panel_.rootMotion ? panel_.rootMotionTime : 0);
    WaitForGpu();
    CreateLightGridResources();
    UpdateDescriptors();
    frameLights_ = MakeManyLights(workshop_.scenes[panel_.scene], panel_.grid);
    gridData_ = MakeLightGridConstants(workshop_, panel_.camera, panel_.pathEnabled ? panel_.pathFrame : -1,
                                       width_, height_, panel_.grid, panel_.navigation.Position(),
                                       panel_.navigation.Target(), panel_.navigation.Projection());
    std::memcpy(gridConstantsData_, &gridData_, sizeof(gridData_));
    timing_.pointLights = uint32_t(frameLights_.size());
    timing_.cells = gridCells_;
    timing_.listCapacity = panel_.grid.capacity;
    timing_.lightsPerRoom = panel_.grid.perRoom;
    timing_.lightRadius = panel_.grid.radius;
    timing_.lightIntensity = panel_.grid.intensity;
    timing_.mode = panel_.forward          ? "Forward"
                   : panel_.grid.mode == 0 ? "All"
                   : panel_.grid.mode == 1 ? "Tiled"
                                           : "Clustered";
    const auto &s = workshop_.scenes[panel_.scene];
    auto fill = [&](const WorkshopInstance *instance)
    {
        auto c = MakeDeferredConstants(
            workshop_, panel_.scene, panel_.camera, panel_.pathEnabled ? panel_.pathFrame : -1, width_,
            height_, instance, UINT(std::min(panel_.view, 13)), panel_.direct, panel_.ibl, panel_.shadows,
            panel_.pcf, panel_.bias, panel_.exposure, panel_.intensity, panel_.yaw, panel_.occluder,
            panel_.navigation.Position(), panel_.navigation.Target(), panel_.navigation.Projection());
        c.controls[0] = uint32_t(frameLights_.size());
        return c;
    };
    auto global = fill(nullptr);
    meshCpuFrame_ = 0;
    UpdateMeshData(global);
    std::memcpy(constantsData_, &global, sizeof(global));
    for (UINT i = 0; i < s.instances.size(); ++i)
    {
        auto c = fill(&s.instances[i]);
        std::memcpy(static_cast<uint8_t *>(constantsData_) + (i + 1) * 512, &c, sizeof(c));
    }
    void *lightData = nullptr;
    D3D12_RANGE empty{};
    CheckHR(points_->Map(0, &empty, &lightData), "Light upload");
    if (!frameLights_.empty())
        std::memcpy(lightData, frameLights_.data(), frameLights_.size() * sizeof(WorkshopLight));
    points_->Unmap(0, nullptr);
    for (UINT p = 0; p < 4; ++p)
    {
        PostConstants c{};
        c.settings = {panel_.exposure, panel_.threshold, panel_.strength, float(panel_.view >= 14 ? 2 : 0)};
        c.dimensions = {width_, height_, std::max(1u, (width_ + 1) / 2), std::max(1u, (height_ + 1) / 2)};
        c.flags = {UINT(panel_.bloom && !panel_.meshletView && panel_.view == 0 && panel_.grid.view == 0),
                   p == 1 ? 1u : 0u,
                   options_.testImage == L"black"      ? 1u
                   : options_.testImage == L"constant" ? 2u
                   : options_.testImage == L"impulse"  ? 3u
                                                       : 0u,
                   UINT(panel_.meshletView || panel_.grid.view != 0 ||
                        (panel_.view >= 1 && panel_.view <= 8 && panel_.view != 5))};
        std::memcpy(static_cast<uint8_t *>(postData_) + p * 256, &c, sizeof(c));
    }
    CheckHR(allocator_->Reset(), "Frame allocator");
    CheckHR(commands_->Reset(allocator_.Get(), nullptr), "Frame commands");
    Stamp(18);
    Stamp(24);
    if (MESH_TECHNIQUE == 5 || !options_.benchmark)
        ResetMeshStatistics();
    PrepareMeshIndirect();
    Stamp(25);
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
    BuildLightLists();
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
    if (MESH_TECHNIQUE != 4 && !panel_.meshletView && panel_.grid.view == 0)
        commands_->DrawInstanced(3, 1, 0, 0);
    Stamp(7);
    Stamp(8);
    if (panel_.grid.view == 0)
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
    if (!options_.benchmark)
        CopyMeshStatistics();
    Stamp(19);
    auto after = Transition(targets_[renderedBuffer_].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                            D3D12_RESOURCE_STATE_PRESENT);
    commands_->ResourceBarrier(1, &after);
    CopyGridStatistics();
    commands_->ResolveQueryData(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 26, queryReadback_.Get(), 0);
    CheckHR(commands_->Close(), "Frame close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    CheckHR(swapchain_->Present(options_.benchmark ? 0 : 1, 0), "Present");
    WaitForGpu();
    void *data = nullptr;
    D3D12_RANGE range{0, 208};
    CheckHR(queryReadback_->Map(0, &range, &data), "Query map");
    const auto *ticks = static_cast<const uint64_t *>(data);
    std::array<double, GpuPassCount> values{};
    for (UINT i = 0; i < GpuPassCount; ++i)
        values[i] = double(ticks[i * 2 + 1] - ticks[i * 2]) * 1000 / timestampFrequency_;
    double meshCullMs = SKIN_TECHNIQUE == 2 ? double(ticks[25] - ticks[24]) * 1000 / timestampFrequency_ : 0;
    queryReadback_->Unmap(0, nullptr);
    timing_.Record(values);
    timing_.RecordMesh(meshCullMs, meshCpuFrame_);
    timing_.RecordSkin(skinning_.poseMilliseconds, skinning_.boundsMilliseconds);
    ReadGridStatistics();
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
    metadata["pointLights"] = frameLights_.size();
    metadata["lightCulling"] = panel_.grid.mode;
    metadata["listCapacity"] = panel_.grid.capacity;
    metadata["cells"] = gridCells_;
    DumpLightGrid(directory);
    DumpMeshData(directory, metadata);
    DumpSkinningData(directory);
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
    for (auto &p : meshDirect_)
        p.Reset();
    for (auto &p : meshTask_)
        p.Reset();
    meshReset_.Reset();
    meshCull_.Reset();
    meshFinalize_.Reset();
    meshSignature_.Reset();
    meshRoot_.Reset();
    meshCommands_.Reset();
    for (auto &b : meshBuffers_)
        b.Reset();
    meshVisible_.Reset();
    meshArgs_.Reset();
    meshReadback_.Reset();
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
    if (gridConstantsData_ && gridConstants_)
        gridConstants_->Unmap(0, nullptr);
    gridConstantsData_ = nullptr;
    gridConstants_.Reset();
    gridStatsReadback_.Reset();
    for (auto &b : gridBuffers_)
        b.Reset();
    tileDepthPipeline_.Reset();
    cullPipeline_.Reset();
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

void Sample::CreateLightGridResources()
{
    uint32_t count = LightGridCellCount(width_, height_, panel_.grid.mode);
    if (count == gridCells_ && gridBuffers_[0])
        return;
    gridCells_ = count;
    uint32_t tiles = ((width_ + 15) / 16) * ((height_ + 15) / 16);
    const uint64_t bytes[] = {uint64_t(count) * 8, uint64_t(count) * LightGridStride * 4, uint64_t(tiles) * 8,
                              uint64_t(tiles) * 4};
    for (UINT i = 0; i < 4; ++i)
    {
        gridBuffers_[i].Reset();
        D3D12_HEAP_PROPERTIES h{};
        h.Type = D3D12_HEAP_TYPE_DEFAULT;
        auto d = BufferDescription(bytes[i]);
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        gridStates_[i] = D3D12_RESOURCE_STATE_COMMON;
        CheckHR(device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, gridStates_[i], nullptr,
                                                 IID_PPV_ARGS(&gridBuffers_[i])),
                "Light-list buffer");
    }
    gridStatsReadback_.Reset();
    D3D12_HEAP_PROPERTIES h{};
    h.Type = D3D12_HEAP_TYPE_READBACK;
    auto d = BufferDescription(uint64_t(count) * 8);
    CheckHR(device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr, IID_PPV_ARGS(&gridStatsReadback_)),
            "Grid statistics readback");
}
void Sample::BuildLightLists()
{
    Stamp(20);
    if (!panel_.forward && panel_.grid.mode != 0)
    {
        for (UINT i = 0; i < 4; ++i)
            Move(gridBuffers_[i].Get(), gridStates_[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        // A global UAV barrier also orders last frame's mask reads before this frame's overwrite.
        commands_->ResourceBarrier(1, &b);
        commands_->SetPipelineState(tileDepthPipeline_.Get());
        Bind(0, 0, true);
        commands_->Dispatch(gridData_.size[0], gridData_.size[1], 1);
        commands_->ResourceBarrier(1, &b);
    }
    Stamp(21);
    Stamp(22);
    if (!panel_.forward && panel_.grid.mode != 0)
    {
        commands_->SetPipelineState(cullPipeline_.Get());
        Bind(0, 0, true);
        commands_->Dispatch(gridData_.size[0], gridData_.size[1],
                            panel_.grid.mode == 2 ? LightGridSlices : 1);
        // Transition the two written lists to SRVs before lighting reads them.
        for (UINT i = 0; i < 2; ++i)
            Move(gridBuffers_[i].Get(), gridStates_[i], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    Stamp(23);
}
void Sample::CopyGridStatistics()
{
    if (panel_.forward || panel_.grid.mode == 0)
        return;
    Move(gridBuffers_[0].Get(), gridStates_[0], D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands_->CopyBufferRegion(gridStatsReadback_.Get(), 0, gridBuffers_[0].Get(), 0,
                                uint64_t(gridCells_) * 8);
    Move(gridBuffers_[0].Get(), gridStates_[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}
void Sample::ReadGridStatistics()
{
    panel_.gridStatistics = {};
    if (!panel_.forward && panel_.grid.mode != 0)
    {
        void *data = nullptr;
        D3D12_RANGE range{0, size_t(gridCells_) * 8};
        CheckHR(gridStatsReadback_->Map(0, &range, &data), "Grid statistics map");
        panel_.gridStatistics.Read(static_cast<const uint32_t *>(data), gridCells_);
        D3D12_RANGE empty{};
        gridStatsReadback_->Unmap(0, &empty);
    }
    timing_.activeCells = panel_.gridStatistics.active;
    timing_.averageCandidates = panel_.gridStatistics.average;
    timing_.maxCandidates = panel_.gridStatistics.maximum;
    timing_.overflowCells = panel_.gridStatistics.overflow;
}
void Sample::DumpLightGrid(const std::filesystem::path &directory)
{
    std::ofstream(directory / "lights.bin", std::ios::binary)
        .write(reinterpret_cast<const char *>(frameLights_.data()),
               frameLights_.size() * sizeof(WorkshopLight));
    std::ofstream(directory / "grid-constants.bin", std::ios::binary)
        .write(reinterpret_cast<const char *>(&gridData_), sizeof(gridData_));
    if (panel_.forward || panel_.grid.mode == 0)
        return;
    const char *names[] = {"light-headers.bin", "light-indices.bin", "tile-depth-bounds.bin",
                           "tile-slice-masks.bin"};
    for (UINT i = 0; i < 4; ++i)
    {
        auto bytes = gridBuffers_[i]->GetDesc().Width;
        D3D12_HEAP_PROPERTIES h{};
        h.Type = D3D12_HEAP_TYPE_READBACK;
        auto d = BufferDescription(bytes);
        ComPtr<ID3D12Resource> readback;
        CheckHR(device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST,
                                                 nullptr, IID_PPV_ARGS(&readback)),
                "List capture");
        CheckHR(allocator_->Reset(), "List capture allocator");
        CheckHR(commands_->Reset(allocator_.Get(), nullptr), "List capture commands");
        auto previous = gridStates_[i];
        Move(gridBuffers_[i].Get(), gridStates_[i], D3D12_RESOURCE_STATE_COPY_SOURCE);
        commands_->CopyBufferRegion(readback.Get(), 0, gridBuffers_[i].Get(), 0, bytes);
        Move(gridBuffers_[i].Get(), gridStates_[i], previous);
        CheckHR(commands_->Close(), "List capture close");
        ID3D12CommandList *lists[] = {commands_.Get()};
        queue_->ExecuteCommandLists(1, lists);
        WaitForGpu();
        void *data = nullptr;
        D3D12_RANGE range{0, size_t(bytes)};
        CheckHR(readback->Map(0, &range, &data), "List capture map");
        std::ofstream(directory / names[i], std::ios::binary).write(static_cast<const char *>(data), bytes);
        readback->Unmap(0, nullptr);
    }
}

// The existing --test-post automation flag exercises live buffer reallocation here.
void Sample::UpdateLightGridTestState()
{
    if (!options_.testPost)
        return;
    if (panel_.frame == 8)
        panel_.grid.mode = 2;
    if (panel_.frame == 16)
        panel_.grid.mode = 0;
    if (panel_.frame == 24)
        panel_.Reset(workshop_);
}
void Sample::CaptureLightGridTestState()
{
    if (!options_.testPost || options_.screenshot.empty())
        return;
    const char *suffix = panel_.frame == 6    ? "-switch-baseline"
                         : panel_.frame == 13 ? "-switch-cluster"
                         : panel_.frame == 21 ? "-switch-all"
                         : panel_.frame == 30 ? "-switch-reset"
                                              : nullptr;
    if (suffix)
        OnScreenshot(options_.screenshot.parent_path() /
                     (options_.screenshot.stem().string() + suffix + ".png"));
}
void Sample::CreateMeshResources()
{
    meshData_ = BuildMeshletData(workshop_);
    CreateSkinResources();
    candidateCapacity_ = 1;
    for (size_t s = 0; s < workshop_.scenes.size(); ++s)
        candidateCapacity_ = std::max(candidateCapacity_,
                                      uint32_t(MakeMeshCandidates(workshop_, meshData_, int(s), 1).size()));
    const void *data[] = {nullptr,
                          meshData_.vertices.data(),
                          meshData_.indices.data(),
                          meshData_.meshlets.data(),
                          meshData_.meshletVertices.data(),
                          meshData_.meshletIndices.data(),
                          meshData_.primitives.data(),
                          meshData_.materials.data(),
                          nullptr,
                          nullptr};
    const size_t counts[] = {2,
                             meshData_.vertices.size(),
                             meshData_.indices.size(),
                             meshData_.meshlets.size(),
                             meshData_.meshletVertices.size(),
                             meshData_.meshletIndices.size(),
                             meshData_.primitives.size(),
                             meshData_.materials.size(),
                             1810,
                             candidateCapacity_};
    const uint32_t strides[] = {128, 48, 4, 64, 4, 4, 32, 80, 144, 16};
    for (UINT i = 0; i < 10; ++i)
        meshBuffers_[i] =
            UploadBuffer(counts[i] ? data[i] : nullptr, std::max(size_t(1), counts[i]) * strides[i]);
    auto make = [&](ComPtr<ID3D12Resource> &out, uint64_t size, D3D12_HEAP_TYPE type)
    {
        D3D12_HEAP_PROPERTIES h{};
        h.Type = type;
        auto d = BufferDescription(size);
        if (type == D3D12_HEAP_TYPE_DEFAULT)
            d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        CheckHR(device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d,
                                                 type == D3D12_HEAP_TYPE_READBACK
                                                     ? D3D12_RESOURCE_STATE_COPY_DEST
                                                     : D3D12_RESOURCE_STATE_COMMON,
                                                 nullptr, IID_PPV_ARGS(&out)),
                "Mesh writable buffer");
    };
    make(meshVisible_, uint64_t(candidateCapacity_) * 16, D3D12_HEAP_TYPE_DEFAULT);
    make(meshArgs_, 256, D3D12_HEAP_TYPE_DEFAULT);
    make(meshReadback_, 256, D3D12_HEAP_TYPE_READBACK);
    meshArgsState_ = D3D12_RESOURCE_STATE_COMMON;
}
void Sample::UpdateMeshDescriptors()
{
    UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu = textureHeap_->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += meshSlot_ * stride;
    const uint32_t sizes[] = {128, 48, 4, 64, 4, 4, 32, 80, 144, 16};
    for (UINT i = 0; i < 11; ++i)
    {
        auto resource = i == 10 ? meshArgs_ : meshBuffers_[i];
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
        v.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.Format = DXGI_FORMAT_UNKNOWN;
        v.Buffer.StructureByteStride = i == 10 ? 4 : sizes[i];
        v.Buffer.NumElements = UINT(resource->GetDesc().Width / v.Buffer.StructureByteStride);
        device_->CreateShaderResourceView(resource.Get(), &v, cpu);
        cpu.ptr += stride;
    }
    for (UINT i = 0; i < 64; ++i)
    {
        auto image = textures_[i < textures_.size() ? i : 0];
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
        v.Format = image->GetDesc().Format;
        v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.Texture2D.MipLevels = image->GetDesc().MipLevels;
        device_->CreateShaderResourceView(image.Get(), &v, cpu);
        cpu.ptr += stride;
    }
    for (UINT i = 0; i < 2; ++i)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
        u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        if (i == 0)
        {
            u.Format = DXGI_FORMAT_UNKNOWN;
            u.Buffer.StructureByteStride = 8;
            u.Buffer.NumElements = candidateCapacity_ * 2;
        }
        else
        {
            u.Format = DXGI_FORMAT_R32_TYPELESS;
            u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            u.Buffer.NumElements = 64;
        }
        device_->CreateUnorderedAccessView(i == 0 ? meshVisible_.Get() : meshArgs_.Get(), nullptr, &u, cpu);
        cpu.ptr += stride;
    }
    for (UINT i = 0; i < 3; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Buffer.StructureByteStride = i == 0 ? 32 : i == 1 ? 64 : 16;
        view.Buffer.NumElements = UINT(skinBuffers_[i]->GetDesc().Width / view.Buffer.StructureByteStride);
        device_->CreateShaderResourceView(skinBuffers_[i].Get(), &view, cpu);
        cpu.ptr += stride;
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC cache{};
    cache.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    cache.Buffer.StructureByteStride = 48;
    cache.Buffer.NumElements = UINT(skinCache_->GetDesc().Width / 48);
    device_->CreateUnorderedAccessView(skinCache_.Get(), nullptr, &cache, cpu);
}
void Sample::UpdateMeshData(const DeferredConstants &global)
{
    timing_.actors = panel_.scene ? 64 : 1;
    timing_.vertices = skinning_.vertexCount;
    timing_.joints = skinning_.jointCount;
    timing_.animation = skinning_.model.clips[panel_.animation].name;
    timing_.renderPath = panel_.renderPath;
    timing_.culling = panel_.culling;
    timing_.meshletCount = uint32_t(meshData_.meshlets.size());
    meshInstances_ = MakeMeshInstances(workshop_, panel_.scene, panel_.occluder);
    skinning_.Update(size_t(panel_.animation), panel_.animationTime, panel_.loop, panel_.scene, meshData_,
                     meshInstances_, SKIN_TECHNIQUE == 3 && panel_.renderPath == 2 && panel_.culling != 0);
    UpdateSkinBuffers();
    meshCandidates_ = MakeMeshCandidates(workshop_, meshData_, panel_.scene, panel_.preset);
    timing_.candidateCount = uint32_t(meshCandidates_.size());
    meshFrames_[0] = MakeMeshFrame(global, true, uint32_t(meshCandidates_.size()), uint32_t(panel_.culling),
                                   panel_.meshletView, !options_.benchmark);
    meshFrames_[1] = MakeMeshFrame(global, false, uint32_t(meshCandidates_.size()), uint32_t(panel_.culling),
                                   panel_.meshletView, !options_.benchmark);
    auto copy = [&](UINT slot, const void *data, size_t bytes)
    {
        void *mapped = nullptr;
        D3D12_RANGE empty{};
        CheckHR(meshBuffers_[slot]->Map(0, &empty, &mapped), "Mesh CPU data map");
        if (bytes)
            std::memcpy(mapped, data, bytes);
        meshBuffers_[slot]->Unmap(0, nullptr);
    };
    copy(0, meshFrames_.data(), sizeof(meshFrames_));
    copy(8, meshInstances_.data(), meshInstances_.size() * sizeof(MeshInstance));
    copy(9, meshCandidates_.data(), meshCandidates_.size() * 16);
    if (meshStatisticsReady_)
    {
        void *p = nullptr;
        D3D12_RANGE range{0, 256};
        CheckHR(meshReadback_->Map(0, &range, &p), "Mesh statistics");
        auto u = static_cast<const uint32_t *>(p);
        panel_.candidateCount = uint32_t(meshCandidates_.size());
        panel_.visibleCount = panel_.renderPath == 3   ? u[0] + u[8]
                              : panel_.renderPath == 2 ? u[37]
                                                       : uint32_t(meshCandidates_.size());
        meshReadback_->Unmap(0, nullptr);
    }
}
void Sample::BindMesh(int pass, UINT instance, UINT base, UINT mode, bool compute)
{
    Bind(0, 0, compute);
    UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto h = textureHeap_->GetGPUDescriptorHandleForHeapStart();
    h.ptr += meshSlot_ * stride;
    UINT push[] = {instance, base, UINT(pass), mode};
    if (compute)
    {
        commands_->SetComputeRootSignature(meshRoot_.Get());
        commands_->SetComputeRootConstantBufferView(0, constants_->GetGPUVirtualAddress());
        auto legacy = textureHeap_->GetGPUDescriptorHandleForHeapStart();
        commands_->SetComputeRootDescriptorTable(1, legacy);
        commands_->SetComputeRootConstantBufferView(3, gridConstants_->GetGPUVirtualAddress());
        auto lists = legacy;
        lists.ptr += 16 * stride;
        commands_->SetComputeRootDescriptorTable(4, lists);
        commands_->SetComputeRoot32BitConstants(6, 4, push, 0);
        commands_->SetComputeRootDescriptorTable(7, h);
        h.ptr += 11 * stride;
        commands_->SetComputeRootDescriptorTable(8, h);
        h.ptr += 64 * stride;
        commands_->SetComputeRootDescriptorTable(9, h);
        h.ptr += 2 * stride;
        commands_->SetComputeRootDescriptorTable(10, h);
        h.ptr += 3 * stride;
        commands_->SetComputeRootDescriptorTable(11, h);
    }
    else
    {
        commands_->SetGraphicsRootSignature(meshRoot_.Get());
        commands_->SetGraphicsRootConstantBufferView(0, constants_->GetGPUVirtualAddress());
        auto legacy = textureHeap_->GetGPUDescriptorHandleForHeapStart();
        commands_->SetGraphicsRootDescriptorTable(1, legacy);
        commands_->SetGraphicsRootConstantBufferView(3, gridConstants_->GetGPUVirtualAddress());
        auto lists = legacy;
        lists.ptr += 16 * stride;
        commands_->SetGraphicsRootDescriptorTable(4, lists);
        commands_->SetGraphicsRoot32BitConstants(6, 4, push, 0);
        commands_->SetGraphicsRootDescriptorTable(7, h);
        h.ptr += 11 * stride;
        commands_->SetGraphicsRootDescriptorTable(8, h);
        h.ptr += 64 * stride;
        commands_->SetGraphicsRootDescriptorTable(9, h);
        h.ptr += 2 * stride;
        commands_->SetGraphicsRootDescriptorTable(10, h);
        h.ptr += 3 * stride;
        commands_->SetGraphicsRootDescriptorTable(11, h);
    }
}
void Sample::ResetMeshStatistics()
{
    Move(meshVisible_.Get(), meshVisibleState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Move(meshArgs_.Get(), meshArgsState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    BindMesh(1, 0, meshDispatchWidth_, 3, true);
    commands_->SetPipelineState(meshReset_.Get());
    commands_->Dispatch(1, 1, 1);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = meshArgs_.Get();
    commands_->ResourceBarrier(1, &b);
}
void Sample::DrawObjects(int pass)
{
    MeshCpuTimer cpu{meshCpuFrame_};
    if (pass == 3)
        return;
    const auto &scene = workshop_.scenes[panel_.scene];
    if (panel_.renderPath == 3)
    {
        for (UINT bin = 0; bin < 2; ++bin)
        {
            BindMesh(pass, 0, meshDispatchWidth_, 3 | (bin << 8), false);
            commands_->SetPipelineState(meshDirect_[pass * 2 + bin].Get());
            commands_->ExecuteIndirect(meshSignature_.Get(), 1, meshArgs_.Get(), bin * 32 + 16, nullptr, 0);
        }
        return;
    }
    for (UINT i = 0; i < scene.instances.size(); ++i)
    {
        const auto &instance = scene.instances[i];
        const auto &primitive = meshData_.primitives[instance.primitive];
        const auto &material = workshop_.materials[primitive.offsets.z];
        if (!panel_.Includes(material.alpha))
            continue;
        UINT bin = material.doubleSided ? 1u : 0u;
        if (panel_.renderPath == 0)
        {
            BindMesh(pass, i, 0, 0, false);
            commands_->SetPipelineState((pass == 0   ? shadowPipelines_
                                         : pass == 1 ? gPipelines_
                                                     : forwardPipelines_)[bin]
                                            .Get());
            commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            if constexpr (SKIN_TECHNIQUE == 2)
            {
                bool floor = meshInstances_[i].primitive.y == UINT32_MAX;
                uint64_t offset = (floor ? 0 : uint64_t(meshInstances_[i].primitive.y)) + primitive.offsets.x;
                D3D12_VERTEX_BUFFER_VIEW view{
                    (floor ? meshBuffers_[1].Get() : skinCache_.Get())->GetGPUVirtualAddress() + offset * 48,
                    UINT(workshop_.primitives[instance.primitive].vertices.size() * 48), 48};
                commands_->IASetVertexBuffers(0, 1, &view);
            }
            commands_->IASetIndexBuffer(&geometry_[instance.primitive].ib);
            commands_->DrawIndexedInstanced(primitive.offsets.w, 1, 0, 0, 0);
        }
        else
        {
            bool task = panel_.renderPath == 2;
            UINT count = task ? (primitive.meshlets.y + 31) / 32 : primitive.meshlets.y;
            BindMesh(pass, i, task ? 0 : primitive.meshlets.x, task ? 2 : 1, false);
            commands_->SetPipelineState((task ? meshTask_ : meshDirect_)[pass * 2 + bin].Get());
            meshCommands_->DispatchMesh(count, 1, 1);
        }
    }
}
void Sample::CreateMeshPipeline()
{
    D3D12_DESCRIPTOR_RANGE ranges[] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 15, 0, 0, 0},  {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 15, 0, 0},  {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 1, 0, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 11, 20, 0, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 64, 32, 0, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 5, 0, 0},   {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 96, 0, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 7, 0, 0}};
    D3D12_ROOT_PARAMETER p[12]{};
    p[0].ParameterType = p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    p[3].Descriptor.ShaderRegister = 1;
    for (UINT i = 0; i < 2; ++i)
    {
        p[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p[i + 1].DescriptorTable = {1, &ranges[i]};
    }
    p[4].ParameterType = p[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[4].DescriptorTable = {1, &ranges[2]};
    p[5].DescriptorTable = {1, &ranges[3]};
    p[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[6].Constants = {2, 0, 4};
    for (UINT i = 0; i < 5; ++i)
    {
        p[7 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p[7 + i].DescriptorTable = {1, &ranges[4 + i]};
    }
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (UINT i = 0; i < 2; ++i)
    {
        samplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW =
            i ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[i].ShaderRegister = i;
        samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
    }
    D3D12_ROOT_SIGNATURE_DESC root{12, p, 2, samplers,
                                   D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> b, e;
    HRESULT hr = D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &b, &e);
    if (FAILED(hr) && e)
        Trace(std::string(static_cast<const char *>(e->GetBufferPointer()), e->GetBufferSize()));
    CheckHR(hr, "Mesh root serialize");
    CheckHR(
        device_->CreateRootSignature(0, b->GetBufferPointer(), b->GetBufferSize(), IID_PPV_ARGS(&meshRoot_)),
        "Mesh root");
    auto shader = [&](const wchar_t *n)
    { return ReadBinary(AssetPath(std::filesystem::path(L"Shaders") / (std::wstring(n) + L".cso"))); };
    auto compute = [&](ComPtr<ID3D12PipelineState> &out, const wchar_t *n)
    {
        auto bytes = shader(n);
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
        d.pRootSignature = meshRoot_.Get();
        d.CS = {bytes.data(), bytes.size()};
        CheckHR(device_->CreateComputePipelineState(&d, IID_PPV_ARGS(&out)), "Mesh compute pipeline");
    };
    compute(meshReset_, L"ResetMeshlets");
    if (MESH_TECHNIQUE == 5)
    {
        compute(meshCull_, L"CullMeshlets");
        compute(meshFinalize_, L"FinalizeMeshlets");
        D3D12_INDIRECT_ARGUMENT_DESC argument{};
        argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
        D3D12_COMMAND_SIGNATURE_DESC d{12, 1, &argument, 0};
        CheckHR(device_->CreateCommandSignature(&d, nullptr, IID_PPV_ARGS(&meshSignature_)),
                "DispatchMesh command signature");
    }
    if constexpr (SKIN_TECHNIQUE == 2)
        compute(skinCompute_, L"SkinningCompute");

#if SKIN_TECHNIQUE == 3
    struct Stream
    {
        MeshStreamObject<ID3D12RootSignature *, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE> root;
        MeshStreamObject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS> amplification;
        MeshStreamObject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS> mesh;
        MeshStreamObject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS> pixel;
        MeshStreamObject<D3D12_RASTERIZER_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER> raster;
        MeshStreamObject<D3D12_BLEND_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND> blend;
        MeshStreamObject<D3D12_DEPTH_STENCIL_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL> depth;
        MeshStreamObject<UINT, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK> mask;
        MeshStreamObject<D3D12_PRIMITIVE_TOPOLOGY_TYPE,
                         D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY>
            topology;
        MeshStreamObject<D3D12_RT_FORMAT_ARRAY, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS>
            targets;
        MeshStreamObject<DXGI_FORMAT, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT> depthFormat;
        MeshStreamObject<DXGI_SAMPLE_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC> sample;
    };
    ComPtr<ID3D12Device2> meshDevice;
    CheckHR(device_.As(&meshDevice), "Mesh pipeline stream device");
    for (UINT task = 0; task < 2; ++task)
    {
        if (task && MESH_TECHNIQUE < 6)
            continue;
        auto m = shader(task ? L"TaskMesh" : L"MeshletMesh");
        std::vector<uint8_t> a;
        if (task)
            a = shader(L"MeshletTask");
        for (UINT pass = 0; pass < 3; ++pass)
            for (UINT bin = 0; bin < 2; ++bin)
            {
                auto ps = shader(pass == 0   ? L"MeshShadowPixel"
                                 : pass == 1 ? L"MeshGBufferPixel"
                                             : L"MeshForwardPixel");
                Stream s{};
                s.root.value = meshRoot_.Get();
                s.mesh.value = {m.data(), m.size()};
                s.pixel.value = {ps.data(), ps.size()};
                if (task)
                    s.amplification.value = {a.data(), a.size()};
                s.raster.value.FillMode = D3D12_FILL_MODE_SOLID;
                s.raster.value.CullMode = bin ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
                s.raster.value.DepthClipEnable = TRUE;
                s.depth.value.DepthEnable = TRUE;
                s.depth.value.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
                s.depth.value.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
                for (auto &target : s.blend.value.RenderTarget)
                    target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                s.mask.value = UINT_MAX;
                s.topology.value = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                s.depthFormat.value = DXGI_FORMAT_D32_FLOAT;
                s.sample.value = {1, 0};
                if (pass == 1)
                {
                    s.targets.value.NumRenderTargets = 4;
                    s.targets.value.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                    s.targets.value.RTFormats[1] = DXGI_FORMAT_R16G16_SNORM;
                    s.targets.value.RTFormats[2] = DXGI_FORMAT_R8G8_UNORM;
                    s.targets.value.RTFormats[3] = DXGI_FORMAT_R11G11B10_FLOAT;
                }
                else if (pass == 2)
                {
                    s.targets.value.NumRenderTargets = 1;
                    s.targets.value.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
                }
                D3D12_PIPELINE_STATE_STREAM_DESC description{sizeof(s), &s};
                auto &out = (task ? meshTask_ : meshDirect_)[pass * 2 + bin];
                CheckHR(meshDevice->CreatePipelineState(&description, IID_PPV_ARGS(&out)),
                        "Mesh pipeline stream");
            }
    }

#endif
}
void Sample::CopyMeshStatistics()
{
    auto before = meshArgsState_;
    Move(meshArgs_.Get(), meshArgsState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands_->CopyBufferRegion(meshReadback_.Get(), 0, meshArgs_.Get(), 0, 256);
    Move(meshArgs_.Get(), meshArgsState_, before);
    meshStatisticsReady_ = true;
}

void Sample::PrepareMeshIndirect()
{

    if constexpr (SKIN_TECHNIQUE == 2)
    {
        Move(skinCache_.Get(), skinCacheState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        uint32_t actors = panel_.scene ? 64 : 1;
        for (uint32_t actor = 0; actor < actors; ++actor)
        {
            BindMesh(1, actor * UINT(skinning_.model.primitives.size()), skinning_.vertexCount, 0, true);
            commands_->SetPipelineState(skinCompute_.Get());
            commands_->Dispatch((skinning_.vertexCount + 63) / 64, 1, 1);
        }
        Move(skinCache_.Get(), skinCacheState_, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    }
}
void Sample::DumpMeshData(const std::filesystem::path &directory, nlohmann::json &metadata)
{
    CaptureBytes(directory / "mesh-vertices.bin", meshData_.vertices.data(),
                 meshData_.vertices.size() * sizeof(MeshVertex));
    CaptureBytes(directory / "mesh-original-indices.bin", meshData_.indices.data(),
                 meshData_.indices.size() * 4);
    CaptureBytes(directory / "mesh-global.bin", constantsData_, sizeof(DeferredConstants));
    CaptureBytes(directory / "mesh-frames.bin", meshFrames_.data(), sizeof(meshFrames_));
    CaptureBytes(directory / "mesh-instances.bin", meshInstances_.data(),
                 meshInstances_.size() * sizeof(MeshInstance));
    CaptureBytes(directory / "meshlets.bin", meshData_.meshlets.data(),
                 meshData_.meshlets.size() * sizeof(Meshlet));
    CaptureBytes(directory / "mesh-candidates.bin", meshCandidates_.data(), meshCandidates_.size() * 16);
    CaptureBytes(directory / "mesh-primitives.bin", meshData_.primitives.data(),
                 meshData_.primitives.size() * sizeof(MeshPrimitive));
    CaptureBytes(directory / "mesh-materials.bin", meshData_.materials.data(),
                 meshData_.materials.size() * sizeof(MeshMaterial));
    auto read = [&](ID3D12Resource *resource, D3D12_RESOURCE_STATES &state, const char *name)
    {
        WaitForGpu();
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        auto desc = BufferDescription(resource->GetDesc().Width);
        ComPtr<ID3D12Resource> output;
        CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&output)),
                "Mesh diagnostic readback");
        CheckHR(allocator_->Reset(), "Mesh diagnostic allocator");
        CheckHR(commands_->Reset(allocator_.Get(), nullptr), "Mesh diagnostic commands");
        auto before = state;
        Move(resource, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commands_->CopyBufferRegion(output.Get(), 0, resource, 0, desc.Width);
        Move(resource, state, before);
        CheckHR(commands_->Close(), "Mesh diagnostic close");
        ID3D12CommandList *list[] = {commands_.Get()};
        queue_->ExecuteCommandLists(1, list);
        WaitForGpu();
        void *mapped = nullptr;
        D3D12_RANGE range{0, SIZE_T(desc.Width)};
        CheckHR(output->Map(0, &range, &mapped), "Mesh diagnostic map");
        CaptureBytes(directory / name, mapped, size_t(desc.Width));
        output->Unmap(0, nullptr);
    };
    read(meshArgs_.Get(), meshArgsState_, "mesh-arguments.bin");
    read(meshVisible_.Get(), meshVisibleState_, "mesh-visible.bin");
    metadata["mesh"] = {{"renderPath", panel_.renderPath},
                        {"culling", panel_.culling},
                        {"meshletCount", meshData_.meshlets.size()},
                        {"candidateCount", meshCandidates_.size()},
                        {"candidateCapacity", candidateCapacity_},
                        {"dispatchWidth", meshDispatchWidth_},
                        {"maxVertices", 64},
                        {"maxTriangles", 96}};
}

void Sample::CreateSkinResources()
{
    skinning_.PrepareBounds(meshData_);
    skinBuffers_[0] =
        UploadBuffer(skinning_.influences.data(), skinning_.influences.size() * sizeof(SkinInfluence));
    skinBuffers_[1] = UploadBuffer(skinning_.palettes.data(), skinning_.palettes.size() * 64);
    skinBuffers_[2] = UploadBuffer(skinning_.spheres.data(), skinning_.spheres.size() * 16);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    auto description =
        BufferDescription((SKIN_TECHNIQUE == 2 ? uint64_t(64) * skinning_.vertexCount : 1) * 48);
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description, skinCacheState_,
                                             nullptr, IID_PPV_ARGS(&skinCache_)),
            "Skin cache");
}
void Sample::UpdateSkinBuffers()
{
    for (UINT i = 1; i < 3; ++i)
    {
        void *data = nullptr;
        D3D12_RANGE empty{};
        CheckHR(skinBuffers_[i]->Map(0, &empty, &data), "Skin upload map");
        if (i == 1)
            std::memcpy(data, skinning_.palettes.data(), skinning_.palettes.size() * 64);
        else
            std::memcpy(data, skinning_.spheres.data(), skinning_.spheres.size() * 16);
        skinBuffers_[i]->Unmap(0, nullptr);
    }
}

void Sample::DumpSkinningData(const std::filesystem::path &directory)
{

    uint32_t actors = panel_.scene ? 64u : 1u, step = actors == 64 ? 64u : 1u;
    std::vector<SkinDeformedVertex> reference, actual;
    std::vector<DirectX::XMFLOAT4X4> palette(skinning_.jointCount);
    for (uint32_t actor = 0; actor < actors; ++actor)
    {
        std::copy_n(skinning_.palettes.begin() + actor * skinning_.jointCount, skinning_.jointCount,
                    palette.begin());
        for (uint32_t i = 0; i < skinning_.vertexCount; i += step)
        {
            auto v = meshData_.vertices[i];
            auto inf = skinning_.influences[i];
            SkinVertex source{v.positionU, v.normalV, v.tangent, inf.joints, inf.weights};
            reference.push_back(DeformSkinVertex(source, palette));
        }
    }
    CaptureBytes(directory / "skin-reference.bin", reference.data(), reference.size() * 48);
    CaptureBytes(directory / "skin-palettes.bin", skinning_.palettes.data(),
                 actors * skinning_.jointCount * 64);
    CaptureBytes(directory / "skin-influences.bin", skinning_.influences.data(),
                 skinning_.influences.size() * 32);
    CaptureBytes(directory / "skin-bounds.bin", skinning_.spheres.data(),
                 actors * meshData_.meshlets.size() * 16);

    if constexpr (SKIN_TECHNIQUE == 2)
    {
        uint64_t bytes = uint64_t(actors) * skinning_.vertexCount * 48;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        auto desc = BufferDescription(bytes);
        ComPtr<ID3D12Resource> readback;
        CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&readback)),
                "Skin readback");
        WaitForGpu();
        CheckHR(allocator_->Reset(), "Skin readback allocator");
        CheckHR(commands_->Reset(allocator_.Get(), nullptr), "Skin readback command list");
        Move(skinCache_.Get(), skinCacheState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commands_->CopyBufferRegion(readback.Get(), 0, skinCache_.Get(), 0, bytes);
        Move(skinCache_.Get(), skinCacheState_, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        CheckHR(commands_->Close(), "Skin readback close");
        ID3D12CommandList *lists[] = {commands_.Get()};
        queue_->ExecuteCommandLists(1, lists);
        WaitForGpu();
        void *data = nullptr;
        D3D12_RANGE range{0, size_t(bytes)};
        CheckHR(readback->Map(0, &range, &data), "Skin readback map");
        auto values = static_cast<const SkinDeformedVertex *>(data);
        for (uint32_t actor = 0; actor < actors; ++actor)
            for (uint32_t i = 0; i < skinning_.vertexCount; i += step)
                actual.push_back(values[actor * skinning_.vertexCount + i]);
        readback->Unmap(0, nullptr);
        CaptureBytes(directory / "skin-cache.bin", actual.data(), actual.size() * 48);
    }

    nlohmann::json metadata = {
        {"technique", SKIN_TECHNIQUE},
        {"animation", skinning_.model.clips[panel_.animation].name},
        {"time", panel_.animationTime},
        {"rootMotion", panel_.rootMotion},
        {"rootMotionTime", panel_.rootMotionTime},
        {"rootSpeed", skinning_.rootMotion.speeds[panel_.animation]},
        {"rootOffset", {skinning_.rootOffset.x, skinning_.rootOffset.y, skinning_.rootOffset.z}},
        {"actors", actors},
        {"verticesPerActor", skinning_.vertexCount},
        {"sampleStride", step},
        {"jointCount", skinning_.jointCount},
        {"poseCpuMs", skinning_.poseMilliseconds},
        {"boundsCpuMs", skinning_.boundsMilliseconds},
        {"cacheBytes", SKIN_TECHNIQUE == 2 ? uint64_t(actors) * skinning_.vertexCount * 48 : 0}};
    std::ofstream(directory / "skinning.json") << metadata.dump(2);
}
