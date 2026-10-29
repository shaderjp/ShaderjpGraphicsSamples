#include "Sample.h"
#include "../../Shared/PostProcessCapture.h"
#include "../../Shared/RaytracingFixtures.h"
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
    const std::map<std::wstring, int> modes = {{L"ptlas", 4}, {L"rebuild", 2}, {L"refit", 3}};
    geometryMode_ = 4;
    if (options.clusterPartition != L"spatial" && options.clusterPartition != L"global")
        throw std::runtime_error("--cg-partition: spatial/global");
    if (!options.clusterMode.empty())
    {
        if (!modes.contains(options.clusterMode))
            throw std::runtime_error("Unknown --cg-mode");
        geometryMode_ = modes.at(options.clusterMode);
    }
    if ((CG_TECHNIQUE == 20 && (geometryMode_ == 1 || geometryMode_ == 3)) ||
        (CG_TECHNIQUE == 22 && geometryMode_ == 3))
        throw std::runtime_error("AS mode is not supported by this technique");
    const std::map<std::wstring, int> views = {{L"final", 0}, {L"clusters", 1},  {L"triangles", 2},
                                               {L"lod", 3},   {L"partition", 4}, {L"activity", 5}};
    if (!views.contains(options.clusterView))
        throw std::runtime_error("Unknown --cg-view");
    geometryView_ = views.at(options.clusterView);
    if (!options.clusterTest.empty() && options.clusterTest != L"plane" && options.clusterTest != L"grid")
        throw std::runtime_error("Unknown --cg-test");
    workshop_ = LoadGeometryLab(options, CG_TECHNIQUE);
    geometry_ = MakeClusterSceneData(workshop_, CG_TECHNIQUE, !options.clusterTest.empty());
    auto panelOptions = options;
    panelOptions.benchmark = panelOptions.cameraPath = false;
    panelOptions.testUi = false;
    panel_.Initialize(workshop_, panelOptions);
    paused_ = options.pauseAnimation || options.testUi;
    animationTime_ = options.animationTime;
    UUID features[] = {D3D12ExperimentalShaderModels, D3D12RaytracingPreview};
    CheckHR(D3D12EnableExperimentalFeatures(CG_TECHNIQUE == 23 ? 1 : 2,
                                            CG_TECHNIQUE == 23 ? features + 1 : features, nullptr, nullptr),
            "Enable DXR Preview (Windows Developer Mode is required)");
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
            gpuName_ = std::filesystem::path(description.Description).string();
            break;
        }
    }
    if (!device_)
        throw std::runtime_error("A hardware D3D12 feature level 12_0 device is required");
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_6};
    CheckHR(device_->CheckFeatureSupport1(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel),
                                          D3D12_PREVIEW_SDK_VERSION),
            "Shader Model query");
    if (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_6)
        throw std::runtime_error("Shader Model 6.6 is required; update your GPU driver");
#ifdef _DEBUG
    CheckHR(device_.As(&infoQueue_), "ID3D12InfoQueue");
    Trace("D3D12 Debug Layer enabled");
#endif
    D3D12_FEATURE_DATA_D3D12_OPTIONS_RAYTRACING2 clusters{};
    CheckHR(device_->CheckFeatureSupport1(D3D12_FEATURE_D3D12_OPTIONS_RAYTRACING2, &clusters,
                                          sizeof(clusters), D3D12_PREVIEW_SDK_VERSION),
            "Clusters/PTLAS query");
    if (!clusters.ClustersAndPTLASSupported || options.clusterForceUnsupported)
        throw std::runtime_error("Clusters and PTLAS are unsupported by this adapter/driver");
    if (CG_TECHNIQUE != 23)
    {
        D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_10};
        CheckHR(device_->CheckFeatureSupport1(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm),
                                              D3D12_PREVIEW_SDK_VERSION),
                "SM 6.10 query");
        if (sm.HighestShaderModel < D3D_SHADER_MODEL_6_10)
            throw std::runtime_error("Shader Model 6.10 is required");
    }
    CheckHR(device_.As(&rtDevice_), "ID3D12DeviceRaytracing2");
    Trace("Agility SDK 1.722.0-preview / SDK export 722 / ClustersAndPTLASSupported=1");
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
    CheckHR(commands_.As(&rtCommands_), "ID3D12CommandListRaytracing2");
    CheckHR(commands_->Close(), "Close initial command list");
    CheckHR(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_)
        throw std::runtime_error("CreateEventW failed");
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 rt{};
    CheckHR(device_->CheckFeatureSupport1(D3D12_FEATURE_D3D12_OPTIONS5, &rt, sizeof(rt),
                                          D3D12_PREVIEW_SDK_VERSION),
            "DXR feature query");
    if (rt.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
        throw std::runtime_error("DXR Tier 1.0 is required");
    Trace("GPU: " + gpuName_);
    Trace("DXR Tier: " + std::to_string(rt.RaytracingTier));
    CreateTargets();
    CreateScene();
    CreateSizeResources();
    CreatePipeline();
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
    CheckHR(device_->CheckFeatureSupport1(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support),
                                          D3D12_PREVIEW_SDK_VERSION),
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
    rayData_ = geometry_.rays;
    rayVertices_ = UploadBuffer(rayData_.vertices.data(), rayData_.vertices.size() * sizeof(RayVertex));
    rayIndices_ = UploadBuffer(rayData_.indices.data(), rayData_.indices.size() * 4);
    rayPrimitives_ =
        UploadBuffer(rayData_.primitives.data(), rayData_.primitives.size() * sizeof(RayPrimitive));
    rayMaterials_ = UploadBuffer(rayData_.materials.data(), rayData_.materials.size() * sizeof(RayMaterial));
    for (const auto &im : workshop_.images)
    {
        auto mips = GenerateMipChain(RayTextureImage(im), im.semantic, im.alphaCutoff);
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

    constants_ = UploadBuffer(nullptr, 256);
    D3D12_RANGE empty{};
    CheckHR(constants_->Map(0, &empty, &constantsData_), "Ray constants map");
    postConstants_ = UploadBuffer(nullptr, 4 * 256);
    CheckHR(postConstants_->Map(0, &empty, &postData_), "Post map");
    points_ = UploadBuffer(nullptr, 72 * sizeof(WorkshopLight));
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = 80;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    fontSlot_ = 79;
    CheckHR(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&textureHeap_)), "Ray descriptors");
    heap.NumDescriptors = 12;
    CheckHR(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&postHeap_)), "Post descriptors");
    CreateClusterResources();
    BuildBlas();
    BuildTlas();
}
Sample::ComPtr<ID3D12Resource> Sample::DefaultBuffer(uint64_t size, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS || state == D3D12_RESOURCE_STATE_COPY_DEST)
        state = D3D12_RESOURCE_STATE_COMMON;
    auto desc = BufferDescription(size);
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (state == D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE)
        desc.Flags |= D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
    ComPtr<ID3D12Resource> r;
    CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                             IID_PPV_ARGS(&r)),
            "AS buffer");
    return r;
}
void Sample::CreateSizeResources()
{
    auto make = [&](UINT w, UINT h, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                    D3D12_RESOURCE_STATES state, bool depth)
    {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{depth ? DXGI_FORMAT_D32_FLOAT : format};
        CheckHR(device_->CheckFeatureSupport1(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support),
                                              D3D12_PREVIEW_SDK_VERSION),
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
                "Raytracing image");
        return image;
    };

    for (UINT i = 0; i < 4; ++i)
    {
        hdrStates_[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        hdr_[i] = make(i ? std::max(1u, (width_ + 1) / 2) : width_,
                       i ? std::max(1u, (height_ + 1) / 2) : height_, DXGI_FORMAT_R16G16B16A16_FLOAT,
                       D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       hdrStates_[i], false);
    }
    for (UINT i = 0; i < 2; ++i)
    {
        diagnosticStates_[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        diagnostic_[i] =
            make(width_, height_, DXGI_FORMAT_R32G32B32A32_FLOAT,
                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                 diagnosticStates_[i], false);
    }
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

    auto rtCpu = textureHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 64; ++i)
    {
        srv(textures_[i < textures_.size() ? i : 0].Get(), rtCpu);
        rtCpu.ptr += stride;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC as{};
    as.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    as.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    as.RaytracingAccelerationStructure.Location = tlas_->GetGPUVirtualAddress();
    device_->CreateShaderResourceView(nullptr, &as, rtCpu);
    rtCpu.ptr += stride;
    const std::array<ID3D12Resource *, 6> buffers = {rayVertices_.Get(),   rayIndices_.Get(),
                                                     rayPrimitives_.Get(), rayInstancesBuffer_.Get(),
                                                     rayMaterials_.Get(),  points_.Get()};
    const UINT sizes[] = {sizeof(RayVertex),    4,
                          sizeof(RayPrimitive), sizeof(RayInstance),
                          sizeof(RayMaterial),  sizeof(WorkshopLight)};
    for (UINT i = 0; i < 6; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
        v.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.Buffer.NumElements = UINT(buffers[i]->GetDesc().Width / sizes[i]);
        v.Buffer.StructureByteStride = sizes[i];
        device_->CreateShaderResourceView(buffers[i], &v, rtCpu);
        rtCpu.ptr += stride;
    }
    for (UINT i = 0; i < 3; ++i)
    {
        srv(environment_[i].Get(), rtCpu, true);
        rtCpu.ptr += stride;
    }
    srv(brdf_.Get(), rtCpu);
    rtCpu.ptr += stride;
    D3D12_SHADER_RESOURCE_VIEW_DESC clusters{};
    clusters.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    clusters.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    clusters.Buffer.NumElements = UINT(geometry_.clusters.size());
    clusters.Buffer.StructureByteStride = sizeof(ClusterGeometry);
    device_->CreateShaderResourceView(clusterMetadata_.Get(), &clusters, rtCpu);
    rtCpu.ptr += stride;

    for (UINT i = 0; i < 3; ++i)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC v{};
        v.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        v.Format = i ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
        device_->CreateUnorderedAccessView(i ? diagnostic_[i - 1].Get() : hdr_[0].Get(), nullptr, &v, rtCpu);
        rtCpu.ptr += stride;
    }
    for (UINT p = 0; p < 4; ++p)
    {
        auto cpu = postHeap_->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += p * 3 * stride;
        UINT input = p < 3 ? p : panel_.view >= 7 ? UINT(panel_.view - 6) : 0;
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
        CheckHR(device_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&out)), "Raytracing display pipeline");
    };
    graphics(postPipeline_, L"PostVertex", L"DeferredPostPixel", 5, true);
    auto compute = [&](ComPtr<ID3D12PipelineState> &out, const wchar_t *name, ID3D12RootSignature *root)
    {
        auto bytes =
            ReadBinary(AssetPath(std::filesystem::path(L"Shaders") / (std::wstring(name) + L".cso")));
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
        d.pRootSignature = root;
        d.CS = {bytes.data(), bytes.size()};
        CheckHR(device_->CreateComputePipelineState(&d, IID_PPV_ARGS(&out)), "Bloom compute pipeline");
    };
    if (RT_TECHNIQUE >= 2)
        compute(bloomPipelines_[0], L"Extract", postRoot_.Get());
    if (RT_TECHNIQUE >= 2)
        compute(bloomPipelines_[1], L"Blur", postRoot_.Get());
    CreateRayPipeline();
}
void Sample::CreateRayPipeline()
{
    D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 76, 0, 0, 0},
                                        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, 76}};
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable = {2, ranges};
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (UINT i = 0; i < 2; ++i)
    {
        auto &s = samplers[i];
        s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW =
            i ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        s.ShaderRegister = i;
        s.MaxLOD = D3D12_FLOAT32_MAX;
        s.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    }
    D3D12_ROOT_SIGNATURE_DESC root{2, parameters, 2, samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> bytes, error;
    CheckHR(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &bytes, &error),
            "Ray root serialize");
    CheckHR(device_->CreateRootSignature(0, bytes->GetBufferPointer(), bytes->GetBufferSize(),
                                         IID_PPV_ARGS(&rootSignature_)),
            "Ray global root");
    const wchar_t *names[] = {L"RayGeneration", L"ClosestHit", L"AlphaAnyHit", L"Miss", L"ShadowMiss"};
    std::array<std::vector<uint8_t>, 5> libraries;
    std::array<D3D12_DXIL_LIBRARY_DESC, 5> libraryDesc{};
    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(13);
    for (UINT i = 0; i < 1u; ++i)
    {
        libraries[i] = ReadBinary(
            AssetPath(geometryMode_ < 2 ? L"Shaders/ClusterLibrary.cso" : L"Shaders/TriangleLibrary.cso"));
        libraryDesc[i].DXILLibrary = {libraries[i].data(), libraries[i].size()};
        subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &libraryDesc[i]});
    }
    D3D12_HIT_GROUP_DESC hits[2]{};
    hits[0].HitGroupExport = L"RadianceHit";
    hits[0].Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hits[0].ClosestHitShaderImport = L"ClosestHit";
    hits[0].AnyHitShaderImport = L"AlphaAnyHit";
    hits[1].HitGroupExport = L"ShadowHit";
    hits[1].Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hits[1].AnyHitShaderImport = L"AlphaAnyHit";
    for (UINT i = 0; i < (RT_TECHNIQUE >= 2 ? 2u : 1u); ++i)
        subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hits[i]});
    D3D12_RAYTRACING_SHADER_CONFIG config{16, 8};
    subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &config});
    auto *globalRoot = rootSignature_.Get();
    subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &globalRoot});
    D3D12_RAYTRACING_PIPELINE_CONFIG1 recursion{
        3, geometryMode_ < 2 ? D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_CLUSTERED_GEOMETRY
                             : D3D12_RAYTRACING_PIPELINE_FLAG_NONE};
    subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1, &recursion});
    D3D12_STATE_OBJECT_DESC desc{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE, UINT(subobjects.size()),
                                 subobjects.data()};
    CheckHR(device_->CreateStateObject(&desc, IID_PPV_ARGS(&rayPipeline_)), "Create DXR State Object");
    ComPtr<ID3D12StateObjectProperties> properties;
    CheckHR(rayPipeline_.As(&properties), "State Object properties");
    std::array<uint8_t, 192> table{};
    const wchar_t *exports[] = {L"RayGeneration", L"Miss", L"ShadowMiss", L"RadianceHit", L"ShadowHit"};
    const UINT offsets[] = {0, 64, 96, 128, 160};
    for (UINT i = 0; i < 5; ++i)
    {
        if (RT_TECHNIQUE == 1 && (i == 2 || i == 4))
            continue;
        auto id = properties->GetShaderIdentifier(exports[i]);
        if (!id)
            throw std::runtime_error("Missing DXR shader export");
        std::memcpy(table.data() + offsets[i], id, 32);
    }
    shaderTable_ = UploadBuffer(table.data(), table.size());
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
void Sample::OnUpdate(float seconds)
{
    if (options_.testUi && (renderFrame_ == 4 || renderFrame_ == 8 || renderFrame_ == 12) &&
        !options_.screenshot.empty())
    {
        const wchar_t *suffix = renderFrame_ == 4   ? L"-baseline"
                                : renderFrame_ == 8 ? L"-stepped"
                                                    : L"-reset";
        OnScreenshot(options_.screenshot.parent_path() /
                     (options_.screenshot.stem().wstring() + suffix + L".png"));
    }
    if (options_.testUi)
    {
        auto &io = ImGui::GetIO();
        io.AddFocusEvent(true);
        if (renderFrame_ == 4 || renderFrame_ == 8)
        {
            auto point = renderFrame_ == 4 ? stepPoint_ : resetPoint_;
            io.AddMousePosEvent(point.x, point.y);
            io.AddMouseButtonEvent(0, true);
        }
        if (renderFrame_ == 5 || renderFrame_ == 9)
            io.AddMouseButtonEvent(0, false);
    }
    if (auto p = panel_.TestCapture(); !p.empty())
        OnScreenshot(p);
    ImGui_ImplWin32_NewFrame();
    panel_.QueueTestInput();
    ImGui::NewFrame();
    DrawClusterPanel();
    const float delta =
        options_.fixedDelta > 0 ? options_.fixedDelta : std::max(0.f, seconds - lastUpdateSeconds_);
    lastUpdateSeconds_ = seconds;
    if (!paused_ || step_)
        animationTime_ += step_ ? 1.f / 60.f : delta;
    if (step_)
        forceGeometry_ = true;
    step_ = false;
    const auto &baseCamera =
        panel_.pathEnabled ? workshop_.path.at(panel_.pathFrame) : workshop_.cameras.at(panel_.camera);
    if (panel_.navigation.Update(window_, seconds, baseCamera))
        panel_.pathEnabled = panel_.pathPlaying = false;
    ImGui::Render();
    if (panel_.pathEnabled && panel_.pathPlaying)
        panel_.pathFrame = (panel_.pathFrame + 1) % 1201;
}
void Sample::OnRender()
{
    WaitForGpu();
    bool modeChanged = builtMode_ != geometryMode_;
    geometryBuiltThisFrame_ = false;
    topBuiltThisFrame_ = true;
    builtMode_ = geometryMode_;
    BuildTlas();
    if (modeChanged)
        CreateRayPipeline();
    forceGeometry_ = false;
    UpdateDescriptors();
    auto c = MakeRayConstants(workshop_, panel_.scene, panel_.camera,
                              panel_.pathEnabled ? panel_.pathFrame : -1, width_, height_, UINT(panel_.view),
                              panel_.shadows, panel_.reflections, panel_.direct, panel_.ibl, panel_.intensity,
                              panel_.yaw, panel_.bias, panel_.navigation.Position(),
                              panel_.navigation.Target(), panel_.navigation.Projection());
    c.lighting.z = UINT(geometryView_);
    std::memcpy(constantsData_, &c, sizeof(c));
    void *points = nullptr;
    D3D12_RANGE empty{};
    CheckHR(points_->Map(0, &empty, &points), "Light map");
    const auto &lights = workshop_.scenes[panel_.scene].lights;
    if (!lights.empty())
        std::memcpy(points, lights.data(), lights.size() * sizeof(WorkshopLight));
    points_->Unmap(0, nullptr);
    for (UINT i = 0; i < 4; ++i)
    {
        PostConstants p{};
        p.settings = {panel_.exposure, panel_.threshold, panel_.strength, 0};
        p.dimensions = {width_, height_, (width_ + 1) / 2, (height_ + 1) / 2};
        p.flags = {UINT(RT_TECHNIQUE >= 2 && panel_.bloom && panel_.view == 0 && geometryView_ == 0),
                   UINT(i == 1), 0,
                   UINT(geometryView_ != 0 || RT_TECHNIQUE == 1 || panel_.view == 1 || panel_.view == 2 ||
                        panel_.view == 3 || panel_.view == 5)};
        std::memcpy(static_cast<uint8_t *>(postData_) + i * 256, &p, sizeof(p));
    }
    CheckHR(allocator_->Reset(), "Frame allocator");
    CheckHR(commands_->Reset(allocator_.Get(), nullptr), "Frame commands");
    Move(hdr_[0].Get(), hdrStates_[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (UINT i = 0; i < 2; ++i)
        Move(diagnostic_[i].Get(), diagnosticStates_[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap *heaps[] = {textureHeap_.Get()};
    commands_->SetDescriptorHeaps(1, heaps);
    commands_->SetComputeRootSignature(rootSignature_.Get());
    commands_->SetComputeRootConstantBufferView(0, constants_->GetGPUVirtualAddress());
    commands_->SetComputeRootDescriptorTable(1, textureHeap_->GetGPUDescriptorHandleForHeapStart());
    commands_->SetPipelineState1(rayPipeline_.Get());
    D3D12_DISPATCH_RAYS_DESC dispatch{};
    auto address = shaderTable_->GetGPUVirtualAddress();
    dispatch.RayGenerationShaderRecord = {address, 32};
    dispatch.MissShaderTable = {address + 64, RT_TECHNIQUE >= 2 ? 64u : 32u, 32};
    dispatch.HitGroupTable = {address + 128, RT_TECHNIQUE >= 2 ? 64u : 32u, 32};
    dispatch.Width = width_;
    dispatch.Height = height_;
    dispatch.Depth = 1;
    Stamp(8);
    commands_->DispatchRays(&dispatch);
    Stamp(9);
    Stamp(10);
    for (UINT i = 0; i < 2; ++i)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = diagnostic_[i].Get();
        commands_->ResourceBarrier(1, &barrier);
    }
    Move(hdr_[0].Get(), hdrStates_[0], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    if (RT_TECHNIQUE == 1)
        for (UINT i = 1; i < 4; ++i)
            Move(hdr_[i].Get(), hdrStates_[i], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    if (RT_TECHNIQUE >= 2)
        for (UINT p = 0; p < 3; ++p)
        {
            Move(hdr_[p + 1].Get(), hdrStates_[p + 1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commands_->SetPipelineState(bloomPipelines_[p ? 1 : 0].Get());
            SetPost(p, true);
            commands_->Dispatch(((width_ + 1) / 2 + 7) / 8, ((height_ + 1) / 2 + 7) / 8, 1);
            Move(hdr_[p + 1].Get(), hdrStates_[p + 1], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
        }
    renderedBuffer_ = swapchain_->GetCurrentBackBufferIndex();
    auto before = Transition(targets_[renderedBuffer_].Get(), D3D12_RESOURCE_STATE_PRESENT,
                             D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands_->ResourceBarrier(1, &before);
    auto target = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    target.ptr += renderedBuffer_ * rtvStride_;
    commands_->OMSetRenderTargets(1, &target, FALSE, nullptr);
    D3D12_VIEWPORT viewport{0, 0, float(width_), float(height_), 0, 1};
    D3D12_RECT scissor{0, 0, LONG(width_), LONG(height_)};
    commands_->RSSetViewports(1, &viewport);
    commands_->RSSetScissorRects(1, &scissor);
    commands_->SetPipelineState(postPipeline_.Get());
    SetPost(3, false);
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands_->DrawInstanced(3, 1, 0, 0);
    RenderImGui();
    auto after = Transition(targets_[renderedBuffer_].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                            D3D12_RESOURCE_STATE_PRESENT);
    commands_->ResourceBarrier(1, &after);
    Stamp(11);
    commands_->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 12,
                                timestampReadback_.Get(), 0);
    CheckHR(commands_->Close(), "Frame close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    CheckHR(swapchain_->Present(0, 0), "Present");
    WaitForGpu();
    ReadValidationMessages();
    CollectClusterTimings();
}
void Sample::OnResize(uint32_t width, uint32_t height)
{
    if (!width || !height)
        return;
    WaitForGpu();
    for (auto &target : targets_)
        target.Reset();
    for (auto &g : diagnostic_)
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
                               {"technique", CG_TECHNIQUE},
                               {"scene", workshop_.scenes[panel_.scene].name},
                               {"instances", rayInstances_.size()},
                               {"verticesShared", true}};
    metadata["writes"] = lastWriteCount_;
    metadata["partitionPolicy"] = options_.clusterPartition == L"global" ? "global" : "spatial";
    metadata["mode"] = geometryMode_;
    metadata["time"] = animationTime_;
    metadata["clusters"] = geometry_.clusters.size();
    metadata["asBytes"] = clasBytes_ + blasBytes_ + templateBytes_ + tlas_->GetDesc().Width;
    const char *names[] = {"hdr", "extract", "horizontal", "bloom", "surface", "ids"};
    const UINT sizes[] = {8, 8, 8, 8, 16, 16};
    for (UINT i = 0; i < 6; ++i)
    {
        if (RT_TECHNIQUE == 1 && i >= 1 && i <= 3)
            continue;
        auto image = i < 4 ? hdr_[i].Get() : diagnostic_[i - 4].Get();
        auto &state = i < 4 ? hdrStates_[i] : diagnosticStates_[i - 4];
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
        if (i < 4)
            metadata["images"].push_back(
                CaptureHalfImage(directory, names[i], UINT(d.Width), d.Height, packed));
    }
    metadata["clusterCpuReference"] = nlohmann::json::array();
    for (const auto &c : geometry_.clusters)
        metadata["clusterCpuReference"].push_back(
            {c.offsets.x, c.offsets.y, c.offsets.z, c.offsets.w, c.extra.x, c.extra.y, c.extra.z, c.extra.w});
    CaptureBytes(directory / "rest-vertices.bin", geometry_.rays.vertices.data(),
                 geometry_.rays.vertices.size() * sizeof(RayVertex));
    DumpClusterBuffer(directory / "vertices.bin", rayVertices_.Get(), verticesState_);
    DumpClusterBuffer(directory / "clusters.bin", clusterMetadata_.Get(), metadataState_);
    DumpClusterBuffer(directory / "instances.bin", rayInstancesBuffer_.Get(), instancesState_);
    metadata["geometryBuiltThisFrame"] = geometryBuiltThisFrame_;
    metadata["diagnostics"] = {{"surface", {"u", "v", "distance", "material"}},
                               {"ids", {"cluster", "primitive", "instance", "partition (-1=global)"}}};
    CaptureMetadata(directory, metadata);
}
void Sample::OnDestroy() noexcept
{
    if (!options_.timings.empty() && !timingFrames_.empty())
    {
        try
        {
            // Logical capacities of geometry/build inputs and compute state,
            // including retained source copies. AS results and scratch are separate.
            uint64_t sourceBytes = 0;
            for (const auto &resource :
                 {rayVertices_, rayIndices_, clusterMetadata_, rayInstancesBuffer_, originalRayInstances_,
                  originalRegular_, originalPartitioned_, gpuRegular_, gpuWrites_, gpuInstanceState_,
                  gpuOperation_, zeroOperation_, operationCount_})
                if (resource)
                    sourceBytes += resource->GetDesc().Width;
            nlohmann::json report = {
                {"technique", CG_TECHNIQUE},
                {"gpu", gpuName_},
                {"width", width_},
                {"height", height_},
                {"mode", geometryMode_},
                {"workload",
                 {{"scene", panel_.scene},
                  {"instances", rayInstances_.size()},
                  {"clusters", geometry_.clusters.size()},
                  {"partition", std::filesystem::path(options_.clusterPartition).string()},
                  {"activePercent", options_.clusterActivePercent},
                  {"fixedDelta", options_.fixedDelta},
                  {"animationStart", options_.animationTime}}},
                {"frames", timingFrames_},
                {"initialBuildTimesMs", initialBuildTimes_},
                {"memory",
                 {{"clas", clasBytes_},
                  {"blas", blasBytes_},
                  {"templates", templateBytes_},
                  {"top", tlas_ ? tlas_->GetDesc().Width : 0},
                  {"scratch", scratchBytes_ + (tlasScratch_ ? tlasScratch_->GetDesc().Width : 0)},
                  {"source", sourceBytes},
                  {"geometry", geometry_.rays.vertices.size() * 48 + geometry_.rays.indices.size() * 4}}}};
            std::ofstream out(options_.timings);
            if (!out)
                throw std::runtime_error("Cannot write timings");
            out << report.dump(2);
            if (!out)
                throw std::runtime_error("Cannot write timings");
        }
        catch (const std::exception &e)
        {
            ValidationError(e.what());
        }
    }
    panel_.navigation.Reset();
    try
    {
        if (queue_ && fence_ && fenceEvent_)
            WaitForGpu();

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
    rayPipeline_.Reset();
    shaderTable_.Reset();
    tlas_.Reset();
    blas_.clear();
    rayVertices_.Reset();
    rayIndices_.Reset();
    rayPrimitives_.Reset();
    rayMaterials_.Reset();
    rayInstancesBuffer_.Reset();
    for (auto &r : diagnostic_)
        r.Reset();
    textures_.clear();
    for (auto &h : hdr_)
        h.Reset();
    for (auto &e : environment_)
        e.Reset();
    for (auto &t : targets_)
        t.Reset();
    brdf_.Reset();
    points_.Reset();
    constants_.Reset();
    postConstants_.Reset();
    textureHeap_.Reset();
    postHeap_.Reset();
    for (auto &p : bloomPipelines_)
        p.Reset();
    postPipeline_.Reset();
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

// Preview DXR operations: build arguments remain on the GPU; there is no address readback.
void Sample::BeginBuild()
{
    WaitForGpu();
    CheckHR(allocator_->Reset(), "AS allocator");
    CheckHR(commands_->Reset(allocator_.Get(), nullptr), "AS commands");
}
void Sample::EndBuild()
{
    CheckHR(commands_->Close(), "AS close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitForGpu();
    ReadValidationMessages();
}
void Sample::Uav(ID3D12Resource *resource)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    commands_->ResourceBarrier(1, &barrier);
}
void Sample::Stamp(UINT index)
{
    commands_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
}
void Sample::CreateClusterResources()
{
    D3D12_QUERY_HEAP_DESC query{};
    query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query.Count = 12;
    CheckHR(device_->CreateQueryHeap(&query, IID_PPV_ARGS(&timestamps_)), "Timestamp heap");
    CheckHR(queue_->GetTimestampFrequency(&timestampFrequency_), "Timestamp frequency");
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    auto desc = BufferDescription(96);
    CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&timestampReadback_)),
            "Timestamp readback");
    clusterMetadata_ =
        UploadBuffer(geometry_.clusters.data(), geometry_.clusters.size() * sizeof(ClusterGeometry));
    verticesState_ = metadataState_ = D3D12_RESOURCE_STATE_GENERIC_READ;
}
void Sample::BuildBlas()
{
    // Static triangle BLASes are created once and shared by all three top-level paths.
    std::vector<std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>> descriptions;
    std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO> sizes;
    UINT64 scratchSize = 0;
    blas_.resize(geometry_.ranges.size());
    for (UINT primitive = 0; primitive < geometry_.ranges.size(); ++primitive)
    {
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
        const auto range = geometry_.ranges[primitive];
        for (UINT i = 0; i < range.y; ++i)
        {
            const auto &c = geometry_.clusters[range.x + i];
            D3D12_RAYTRACING_GEOMETRY_DESC g{};
            g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            g.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            g.Triangles.VertexBuffer = {rayVertices_->GetGPUVirtualAddress() + uint64_t(c.offsets.x) * 48,
                                        48};
            g.Triangles.VertexCount = c.offsets.w;
            g.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            g.Triangles.IndexBuffer = rayIndices_->GetGPUVirtualAddress() + uint64_t(c.offsets.y) * 4;
            g.Triangles.IndexCount = c.offsets.z * 3;
            g.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
            geometries.push_back(g);
        }
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.NumDescs = UINT(geometries.size());
        inputs.pGeometryDescs = geometries.data();
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
        device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        if (!info.ResultDataMaxSizeInBytes)
            throw std::runtime_error("Static BLAS prebuild failed");
        blas_[primitive] = DefaultBuffer(info.ResultDataMaxSizeInBytes,
                                         D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        blasBytes_ += info.ResultDataMaxSizeInBytes;
        scratchSize = std::max(scratchSize, info.ScratchDataSizeInBytes);
        descriptions.push_back(std::move(geometries));
        sizes.push_back(info);
    }
    blasScratch_ = DefaultBuffer(scratchSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    scratchBytes_ = scratchSize;
    BeginBuild();
    Stamp(0);
    Stamp(1);
    Stamp(2);
    Stamp(3);
    Stamp(4);
    for (UINT i = 0; i < descriptions.size(); ++i)
    {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
        build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        build.Inputs.NumDescs = UINT(descriptions[i].size());
        build.Inputs.pGeometryDescs = descriptions[i].data();
        build.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        build.DestAccelerationStructureData = blas_[i]->GetGPUVirtualAddress();
        build.ScratchAccelerationStructureData = blasScratch_->GetGPUVirtualAddress();
        commands_->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
        Uav(blas_[i].Get());
        Uav(blasScratch_.Get());
    }
    Stamp(5);
    EndBuild();
    builtMode_ = geometryMode_;
}
void Sample::CreateInstancePipeline()
{
    static_assert(sizeof(D3D12_RTAS_PARTITIONED_TLAS_OPERATION_WRITE_INSTANCE_ARGS) == 104);
    static_assert(offsetof(D3D12_RTAS_PARTITIONED_TLAS_OPERATION_WRITE_INSTANCE_ARGS, PartitionIndex) == 76);
    static_assert(offsetof(D3D12_RTAS_PARTITIONED_TLAS_OPERATION, ArgCount) == 4);
    D3D12_ROOT_PARAMETER p[9]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[0].Constants = {0, 0, 8};
    for (UINT i = 1; i < 4; ++i)
    {
        p[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        p[i].Descriptor.ShaderRegister = i - 1;
    }
    for (UINT i = 4; i < 9; ++i)
    {
        p[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        p[i].Descriptor.ShaderRegister = i - 4;
    }
    D3D12_ROOT_SIGNATURE_DESC desc{9, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> bytes, error;
    CheckHR(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &bytes, &error),
            "Instance root serialize");
    CheckHR(device_->CreateRootSignature(0, bytes->GetBufferPointer(), bytes->GetBufferSize(),
                                         IID_PPV_ARGS(&instanceRoot_)),
            "Instance root");
    auto code = ReadBinary(AssetPath(L"Shaders/UpdateInstances.cso"));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = instanceRoot_.Get();
    pipeline.CS = {code.data(), code.size()};
    CheckHR(device_->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&instancePipeline_)),
            "Instance compute");
}
void Sample::BuildTlas()
{
    WaitForGpu();
    const bool initial = topBuiltMode_ != geometryMode_ || builtScene_ != panel_.scene || !gpuInstanceState_;
    if (initial)
    {
        rayInstances_ = MakeClusterInstances(workshop_, geometry_, panel_.scene);
        const UINT count = UINT(rayInstances_.size());
        const UINT side = panel_.scene ? 128 : 64;
        const UINT partitionSide = side / 4;
        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> regular(count);
        std::vector<D3D12_RTAS_PARTITIONED_TLAS_OPERATION_WRITE_INSTANCE_ARGS> partitioned(count);
        for (UINT i = 0; i < count; ++i)
        {
            const auto &r = rayInstances_[i];
            auto &a = regular[i];
            auto &b = partitioned[i];
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 4; ++col)
                    a.Transform[row][col] = b.Transform[row][col] = r.world.m[col][row];
            a.InstanceID = i;
            a.InstanceMask = 255;
            a.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
            a.AccelerationStructure = blas_[r.primitive.x]->GetGPUVirtualAddress();
            b.InstanceID = b.InstanceIndex = i;
            b.InstanceMask = 255;
            b.InstanceFlags = D3D12_RTAS_PARTITIONED_TLAS_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
            b.AccelerationStructure = a.AccelerationStructure;
            b.PartitionIndex = i ? ((i - 1) % side) / 4 + ((i - 1) / side) / 4 * partitionSide : 0;
        }
        originalRayInstances_ = UploadBuffer(rayInstances_.data(), uint64_t(count) * sizeof(RayInstance));
        originalRegular_ = UploadBuffer(regular.data(), uint64_t(count) * sizeof(regular[0]));
        originalPartitioned_ = UploadBuffer(partitioned.data(), uint64_t(count) * sizeof(partitioned[0]));
        rayInstancesBuffer_ =
            DefaultBuffer(uint64_t(count) * sizeof(RayInstance), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        gpuRegular_ = DefaultBuffer(uint64_t(count) * 64, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        gpuWrites_ = DefaultBuffer(uint64_t(count) * 104, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        gpuInstanceState_ = DefaultBuffer(uint64_t(count) * 8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        gpuOperation_ = DefaultBuffer(24, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_RTAS_PARTITIONED_TLAS_OPERATION empty{};
        empty.Type = D3D12_RTAS_PARTITIONED_TLAS_OPERATION_TYPE_WRITE_INSTANCE;
        empty.ArgData = {gpuWrites_->GetGPUVirtualAddress(), 104};
        zeroOperation_ = UploadBuffer(&empty, sizeof(empty));
        const UINT one = 1;
        operationCount_ = UploadBuffer(&one, 4);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        auto buffer = BufferDescription(4);
        CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&writeCountReadback_)),
                "Write count readback");
        instancesState_ = regularState_ = writesState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        operationState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        tlas_.Reset();
        tlasScratch_.Reset();
        if (geometryMode_ == 4)
        {
            ptlasInputs_ = {};
            ptlasInputs_.Flags = D3D12_RTAS_PARTITIONED_TLAS_FLAG_FAST_TRACE;
            ptlasInputs_.InstanceCount = count;
            ptlasInputs_.PartitionCount = partitionSide * partitionSide;
            // Each panel remains in its original 4x4 cell or the global partition.
            // Partition 0 additionally holds the single static floor instance.
            ptlasInputs_.MaxInstancePerPartitionCount = 17;
            ptlasInputs_.MaxInstanceInGlobalPartitionCount = count - 1;
            D3D12_RTAS_OPERATION_INPUTS input{};
            input.Type = D3D12_RTAS_OPERATION_TYPE_PARTITIONED_TLAS;
            input.pPartitionedTLASInputsDesc = &ptlasInputs_;
            D3D12_RTAS_OPERATION_PREBUILD_INFO info{};
            rtDevice_->GetRTASOperationPrebuildInfo(&input, &info);
            if (!info.ResultDataMaxSizeInBytes)
                throw std::runtime_error("PTLAS capacity prebuild failed");
            tlas_ = DefaultBuffer(info.ResultDataMaxSizeInBytes,
                                  D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
            tlasScratch_ = DefaultBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }
    if (!instancePipeline_)
        CreateInstancePipeline();
    BeginBuild();
    Stamp(0);
    Move(rayInstancesBuffer_.Get(), instancesState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Move(gpuRegular_.Get(), regularState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Move(gpuWrites_.Get(), writesState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Move(gpuOperation_.Get(), operationState_, D3D12_RESOURCE_STATE_COPY_DEST);
    commands_->CopyBufferRegion(gpuOperation_.Get(), 0, zeroOperation_.Get(), 0, 24);
    Move(gpuOperation_.Get(), operationState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    struct Simulation
    {
        float time;
        UINT count, side, percent, global, all, partitionSide, unused;
    } parameters{animationTime_,
                 UINT(rayInstances_.size()),
                 panel_.scene ? 128u : 64u,
                 options_.clusterActivePercent,
                 UINT(options_.clusterPartition == L"global"),
                 UINT(initial),
                 panel_.scene ? 32u : 16u,
                 0};
    commands_->SetComputeRootSignature(instanceRoot_.Get());
    commands_->SetPipelineState(instancePipeline_.Get());
    commands_->SetComputeRoot32BitConstants(0, 8, &parameters, 0);
    commands_->SetComputeRootShaderResourceView(1, originalRayInstances_->GetGPUVirtualAddress());
    commands_->SetComputeRootShaderResourceView(2, originalRegular_->GetGPUVirtualAddress());
    commands_->SetComputeRootShaderResourceView(3, originalPartitioned_->GetGPUVirtualAddress());
    commands_->SetComputeRootUnorderedAccessView(4, rayInstancesBuffer_->GetGPUVirtualAddress());
    commands_->SetComputeRootUnorderedAccessView(5, gpuRegular_->GetGPUVirtualAddress());
    commands_->SetComputeRootUnorderedAccessView(6, gpuWrites_->GetGPUVirtualAddress());
    commands_->SetComputeRootUnorderedAccessView(7, gpuOperation_->GetGPUVirtualAddress());
    commands_->SetComputeRootUnorderedAccessView(8, gpuInstanceState_->GetGPUVirtualAddress());
    commands_->Dispatch((parameters.count + 63) / 64, 1, 1);
    Uav(gpuInstanceState_.Get());
    Move(rayInstancesBuffer_.Get(), instancesState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Move(gpuRegular_.Get(), regularState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Move(gpuWrites_.Get(), writesState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Move(gpuOperation_.Get(), operationState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Stamp(1);
    Stamp(6);
    if (geometryMode_ == 4)
    {
        D3D12_RTAS_PARTITIONED_TLAS_OPERATION_DATA data{};
        data.SourceAccelerationStructureData = initial ? 0 : tlas_->GetGPUVirtualAddress();
        data.DestAccelerationStructureData = tlas_->GetGPUVirtualAddress();
        data.ScratchAccelerationStructureData = tlasScratch_->GetGPUVirtualAddress();
        data.IndirectPartitionedTlasOpCount = operationCount_->GetGPUVirtualAddress();
        data.IndirectPartitionedTlasOps = gpuOperation_->GetGPUVirtualAddress();
        D3D12_RTAS_OPERATION_DESC build{};
        build.Inputs.Type = D3D12_RTAS_OPERATION_TYPE_PARTITIONED_TLAS;
        build.Inputs.pPartitionedTLASInputsDesc = &ptlasInputs_;
        build.pPartitionedTlasOperationData = &data;
        rtCommands_->ExecuteIndirectRTASOperations(1, &build,
                                                   D3D12_EXECUTE_INDIRECT_RTAS_OPERATIONS_FLAG_NONE);
    }
    else
    {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS input{};
        input.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        input.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        input.NumDescs = parameters.count;
        input.InstanceDescs = gpuRegular_->GetGPUVirtualAddress();
        input.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        if (geometryMode_ == 3)
            input.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        if (initial)
        {
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
            device_->GetRaytracingAccelerationStructurePrebuildInfo(&input, &info);
            tlas_ = DefaultBuffer(info.ResultDataMaxSizeInBytes,
                                  D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
            tlasScratch_ =
                DefaultBuffer(std::max(info.ScratchDataSizeInBytes, info.UpdateScratchDataSizeInBytes),
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
        build.Inputs = input;
        build.DestAccelerationStructureData = tlas_->GetGPUVirtualAddress();
        build.ScratchAccelerationStructureData = tlasScratch_->GetGPUVirtualAddress();
        if (!initial && geometryMode_ == 3)
        {
            build.SourceAccelerationStructureData = build.DestAccelerationStructureData;
            build.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        }
        commands_->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    }
    Uav(tlas_.Get());
    Uav(tlasScratch_.Get());
    Stamp(7);
    Move(gpuOperation_.Get(), operationState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands_->CopyBufferRegion(writeCountReadback_.Get(), 0, gpuOperation_.Get(), 4, 4);
    Move(gpuOperation_.Get(), operationState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    EndBuild();
    void *mapped = nullptr;
    D3D12_RANGE range{0, 4};
    CheckHR(writeCountReadback_->Map(0, &range, &mapped), "Write count map");
    lastWriteCount_ = *static_cast<UINT *>(mapped);
    writeCountReadback_->Unmap(0, nullptr);
    if (lastWriteCount_ > rayInstances_.size())
        throw std::runtime_error("PTLAS write capacity exceeded");
    builtScene_ = panel_.scene;
    topBuiltMode_ = geometryMode_;
}

void Sample::CollectClusterTimings()
{
    void *mapped = nullptr;
    D3D12_RANGE range{0, 96};
    CheckHR(timestampReadback_->Map(0, &range, &mapped), "Timestamp map");
    const auto *ticks = static_cast<const uint64_t *>(mapped);
    std::array<double, 6> values{};
    for (UINT i = 0; i < 6; ++i)
        values[i] = double(ticks[i * 2 + 1] - ticks[i * 2]) * 1000 / double(timestampFrequency_);
    timestampReadback_->Unmap(0, nullptr);
    gpuTimes_ = values;
    if (renderFrame_ == 0)
        initialBuildTimes_ = values;
    if (!geometryBuiltThisFrame_)
    {
        if (CG_TECHNIQUE != 23)
            values[0] = 0;
        values[1] = values[2] = 0;
    }
    if (!topBuiltThisFrame_)
        values[3] = 0;
    if (!options_.timings.empty())
        timingFrames_.push_back({{"frame", renderFrame_},
                                 {"time", animationTime_},
                                 {"mode", geometryMode_},
                                 {"vertices_ms", values[0]},
                                 {"clas_ms", values[1]},
                                 {"blas_ms", values[2]},
                                 {"top_ms", values[3]},
                                 {"writes", lastWriteCount_},
                                 {"trace_ms", values[4]},
                                 {"display_ms", values[5]}});
    ++renderFrame_;
}
void Sample::DrawClusterPanel()
{
    if (!panel_.visible)
        return;
    ImGui::SetNextWindowPos({12, 12}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({365, 540}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Geometry Lab / DXR Preview");
    ImGui::Text("Advanced %d / %s", CG_TECHNIQUE, gpuName_.c_str());
    const char *modes[] = {"TLAS rebuild", "TLAS refit", "Partitioned TLAS"};
    int mode = geometryMode_ - 2;
    if (ImGui::Combo("Top-level AS", &mode, modes, 3))
        geometryMode_ = mode + 2;
    int partition = options_.clusterPartition == L"global" ? 1 : 0;
    const char *partitions[] = {"Spatial partitions", "Moving in global partition"};
    if (ImGui::Combo("Partition policy", &partition, partitions, 2))
        options_.clusterPartition = partition ? L"global" : L"spatial";
    int percent = int(options_.clusterActivePercent);
    if (ImGui::SliderInt("Active percent", &percent, 0, 100))
        options_.clusterActivePercent = UINT(percent);
    ImGui::Text("GPU WRITE_INSTANCE records: %u", lastWriteCount_);
    const char *views[] = {"Final", "Clusters", "Triangles", "LOD / boundaries", "Partition", "Activity"};
    ImGui::Combo("Geometry view", &geometryView_, views, 6);
    const char *scenes[] = {"Standard", "Stress"};
    if (ImGui::Combo("Workload", &panel_.scene, scenes, 2))
    {
        panel_.camera = panel_.scene;
        panel_.navigation.Reset();
        forceGeometry_ = true;
    }
    if (CG_TECHNIQUE != 20)
    {
        ImGui::Checkbox("Pause", &paused_);
        ImGui::SameLine();
        if (ImGui::Button("Step"))
        {
            paused_ = true;
            step_ = true;
        }
        {
            auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            stepPoint_ = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
        {
            animationTime_ = options_.animationTime;
            paused_ = true;
            forceGeometry_ = true;
        }
        {
            auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            resetPoint_ = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
        }
    }
    if (CG_TECHNIQUE == 22)
    {
        int lod = options_.clusterLod == 0   ? 0
                  : options_.clusterLod == 2 ? 1
                  : options_.clusterLod == 4 ? 2
                                             : 3;
        const char *levels[] = {"Adaptive", "2", "4", "8"};
        if (ImGui::Combo("Subdivision", &lod, levels, 4))
        {
            options_.clusterLod = lod == 0 ? 0 : 1u << lod;
            forceGeometry_ = true;
        }
    }
    ImGui::Text("Time %.3f s", animationTime_);
    ImGui::Text("%zu clusters / %zu instances", geometry_.clusters.size(), rayInstances_.size());
    ImGui::Text("Last build: vertex %.3f / CLAS %.3f ms", gpuTimes_[0], gpuTimes_[1]);
    ImGui::Text("BLAS %.3f / TLAS %.3f ms", gpuTimes_[2], gpuTimes_[3]);
    ImGui::Text("Trace %.3f ms", gpuTimes_[4]);
    ImGui::Text("AS %.2f MiB / scratch %.2f MiB",
                double(clasBytes_ + blasBytes_ + templateBytes_ + (tlas_ ? tlas_->GetDesc().Width : 0)) /
                    1048576.,
                double(scratchBytes_ + (tlasScratch_ ? tlasScratch_->GetDesc().Width : 0)) / 1048576.);
    ImGui::Checkbox("Shadows", &panel_.shadows);
    ImGui::SameLine();
    ImGui::Checkbox("Reflections", &panel_.reflections);
    ImGui::Checkbox("IBL", &panel_.ibl);
    ImGui::SliderFloat("Exposure", &panel_.exposure, .1f, 3);
    ImGui::TextWrapped("RMB drag: look / WASD: move. AS changes wait for the frame fence.");
    ImGui::End();
}

void Sample::DumpClusterBuffer(const std::filesystem::path &path, ID3D12Resource *source,
                               D3D12_RESOURCE_STATES &state)
{
    const auto bytes = source->GetDesc().Width;
    auto description = BufferDescription(bytes);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    ComPtr<ID3D12Resource> readback;
    CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&readback)),
            "Geometry readback");
    BeginBuild();
    const auto previous = state;
    if (previous != D3D12_RESOURCE_STATE_GENERIC_READ)
        Move(source, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands_->CopyBufferRegion(readback.Get(), 0, source, 0, bytes);
    if (previous != D3D12_RESOURCE_STATE_GENERIC_READ)
        Move(source, state, previous);
    EndBuild();
    void *mapped = nullptr;
    D3D12_RANGE range{0, SIZE_T(bytes)};
    CheckHR(readback->Map(0, &range, &mapped), "Geometry readback map");
    CaptureBytes(path, mapped, size_t(bytes));
    readback->Unmap(0, nullptr);
}
