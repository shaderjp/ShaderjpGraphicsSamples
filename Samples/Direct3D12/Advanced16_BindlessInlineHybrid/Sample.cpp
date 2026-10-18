#include "Sample.h"
#include <cstring>
#include <imgui_impl_win32.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
using namespace tutorial;
using namespace DirectX;
extern "C"
{
    __declspec(dllexport) extern const UINT D3D12SDKVersion = 619;
    __declspec(dllexport) extern const char *D3D12SDKPath = ".\\D3D12\\";
}
namespace
{
D3D12_RESOURCE_DESC BufferDesc(uint64_t bytes)
{
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = std::max(bytes, 4ull);
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}
#pragma warning(push)
#pragma warning(disable : 4324) // Pipeline stream subobjects require pointer alignment.
template <class T, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Kind> struct alignas(void *) Subobject
{
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE kind = Kind;
    T value{};
};
} // namespace
void Sample::OnInit(HWND window, uint32_t width, uint32_t height, const Options &options)
{
    if(options.preset==L"Full")throw std::runtime_error("Inline RT supports OPAQUE/MASK; Full BLEND is unsupported");
    options_ = options;
    window_ = window;
    width_ = width;
    height_ = height;
    timing_.api = "Direct3D12";
    auto start = std::chrono::steady_clock::now();
    data_ = LoadGallery(options_);
    timing_.loadMs = GalleryMs(start);
    panel_.Initialize(data_, options_);
    initialOptions_ = options_;
    initialOptions_.galleryObjects = data_.objects;
    initialOptions_.galleryMaterials = data_.materialCount;
    UINT flags = 0;
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    CheckHR(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)), "D3D12 Debug Layer");
    debug->EnableDebugLayer();
    flags = DXGI_CREATE_FACTORY_DEBUG;
    Trace("D3D12 Debug Layer enabled");
#endif
    CheckHR(CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory_)), "CreateDXGIFactory2");
    for (UINT i = 0;; ++i)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                 IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND)
            break;
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_))))
        {
            timing_.gpuName = std::filesystem::path(desc.Description).string();
            LARGE_INTEGER version{};
            adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version);
            timing_.driver = std::to_string(version.QuadPart);
            break;
        }
    }
    if (!device_)
        throw std::runtime_error("Hardware D3D12 device required");
    D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_6};
    CheckHR(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm)), "Shader Model query");
    D3D12_FEATURE_DATA_D3D12_OPTIONS binding{};
    CheckHR(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &binding, sizeof(binding)),
            "Binding tier query");
    if (sm.HighestShaderModel < D3D_SHADER_MODEL_6_6 ||
        binding.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3)
        throw std::runtime_error("SM6.6 and Resource Binding Tier 3 required");
    if constexpr (GALLERY_TECHNIQUE == 13 || GALLERY_TECHNIQUE == 15)
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 mesh{};
        CheckHR(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &mesh, sizeof(mesh)),
                "Mesh feature query");
        if (mesh.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
            throw std::runtime_error("Mesh Shader required");
    }
    if constexpr (GALLERY_TECHNIQUE >= 14)
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 rt{};
        CheckHR(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &rt, sizeof(rt)), "DXR query");
        if (rt.RaytracingTier < D3D12_RAYTRACING_TIER_1_1)
            throw std::runtime_error("DXR Tier 1.1 required for RayQuery");
    }
#ifdef _DEBUG
    CheckHR(device_.As(&infoQueue_), "InfoQueue");
#endif
    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    CheckHR(device_->CreateCommandQueue(&q, IID_PPV_ARGS(&queue_)), "Command queue");
    CheckHR(queue_->GetTimestampFrequency(&timestampFrequency_), "Timestamp frequency");
    DXGI_SWAP_CHAIN_DESC1 swap{};
    swap.Width = width_;
    swap.Height = height_;
    swap.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap.SampleDesc.Count = 1;
    swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap.BufferCount = 2;
    swap.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> chain;
    CheckHR(factory_->CreateSwapChainForHwnd(queue_.Get(), window_, &swap, nullptr, nullptr, &chain),
            "Swapchain");
    CheckHR(chain.As(&swapchain_), "Swapchain3");
    factory_->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = 6;
    CheckHR(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvHeap_)), "RTV heap");
    rtvStride_ = device_->GetDescriptorHandleIncrementSize(heap.Type);
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap.NumDescriptors = 1;
    CheckHR(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&depthHeap_)), "DSV heap");
    CheckHR(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)),
            "Command allocator");
    CheckHR(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr,
                                       IID_PPV_ARGS(&commands_)),
            "Command list");
    CheckHR(commands_->Close(), "Initial close");
    CheckHR(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "Fence");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_)
        throw std::runtime_error("Fence event");
    D3D12_QUERY_HEAP_DESC queries{};
    queries.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queries.Count = 12;
    CheckHR(device_->CreateQueryHeap(&queries, IID_PPV_ARGS(&timestamps_)), "Timestamp heap");
    timestampReadback_ = DefaultBuffer(96, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_HEAP_TYPE_READBACK);
    timing_.presentMode = options_.benchmark ? "Present(0)" : "Present(1)";
    CreateTargets();
    CreateScene();
    CreateSizeResources();
    CreatePipeline();
    CreateImGui();
    ReadValidationMessages();
    Trace("GPU: " + timing_.gpuName);
}
void Sample::WaitForGpu()
{
    CheckHR(queue_->Signal(fence_.Get(), ++fenceValue_), "Fence signal");
    if (fence_->GetCompletedValue() < fenceValue_)
    {
        CheckHR(fence_->SetEventOnCompletion(fenceValue_, fenceEvent_), "Fence event");
        if (WaitForSingleObject(fenceEvent_, 30000) != WAIT_OBJECT_0)
            throw std::runtime_error("GPU timeout");
    }
}
void Sample::BeginCommands()
{
    CheckHR(allocator_->Reset(), "Allocator reset");
    CheckHR(commands_->Reset(allocator_.Get(), nullptr), "Commands reset");
}
void Sample::Execute()
{
    CheckHR(commands_->Close(), "Commands close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
}
void Sample::Move(ID3D12Resource *resource, D3D12_RESOURCE_STATES &state, D3D12_RESOURCE_STATES next)
{
    if (state == next)
        return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, state, next};
    commands_->ResourceBarrier(1, &b);
    state = next;
}
Sample::ComPtr<ID3D12Resource> Sample::UploadBuffer(const void *data, uint64_t bytes)
{
    D3D12_HEAP_PROPERTIES h{};
    h.Type = D3D12_HEAP_TYPE_UPLOAD;
    auto desc = BufferDesc(bytes);
    ComPtr<ID3D12Resource> r;
    CheckHR(device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&r)),
            "Upload buffer");
    if (data && bytes)
    {
        void *p;
        D3D12_RANGE none{};
        CheckHR(r->Map(0, &none, &p), "Upload map");
        std::memcpy(p, data, size_t(bytes));
        r->Unmap(0, nullptr);
    }
    return r;
}
Sample::ComPtr<ID3D12Resource> Sample::DefaultBuffer(uint64_t bytes, D3D12_RESOURCE_STATES state,
                                                     D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES h{};
    h.Type = type;
    auto desc = BufferDesc(bytes);
    if (type == D3D12_HEAP_TYPE_DEFAULT)
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> r;
    CheckHR(
        device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&r)),
        "Default/readback buffer");
    return r;
}
D3D12_CPU_DESCRIPTOR_HANDLE Sample::Cpu(UINT index) const
{
    auto h = textureHeap_->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(index) * descriptorStride_;
    return h;
}
D3D12_GPU_DESCRIPTOR_HANDLE Sample::Gpu(UINT index) const
{
    auto h = textureHeap_->GetGPUDescriptorHandleForHeapStart();
    h.ptr += UINT64(index) * descriptorStride_;
    return h;
}
void Sample::CreateTargets()
{
    auto h = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC v{};
    v.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    v.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    for (UINT i = 0; i < 2; ++i)
    {
        CheckHR(swapchain_->GetBuffer(i, IID_PPV_ARGS(&targets_[i])), "Backbuffer");
        device_->CreateRenderTargetView(targets_[i].Get(), &v, h);
        h.ptr += rtvStride_;
    }
}
Sample::ComPtr<ID3D12Resource> Sample::LoadTexture(const std::vector<uint32_t> &layers, uint32_t channel)
{
    std::vector<std::vector<Image>> pixels;
    for (auto index : layers)
    {
        const auto &image = data_.images.at(index);
        pixels.push_back(GenerateMipChain(LoadGalleryImage(image), image.semantic, image.alphaCutoff));
    }
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = pixels[0][0].width;
    d.Height = pixels[0][0].height;
    d.DepthOrArraySize = UINT16(layers.size());
    d.MipLevels = UINT16(pixels[0].size());
    d.Format = channel == 0 || channel == 3 ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES h{};
    h.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> image;
    CheckHR(device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr, IID_PPV_ARGS(&image)),
            "Material image");
    timing_.committedImages += device_->GetResourceAllocationInfo(0, 1, &d).SizeInBytes;
    UINT count = UINT(layers.size() * pixels[0].size());
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> feet(count);
    UINT64 bytes;
    device_->GetCopyableFootprints(&d, 0, count, 0, feet.data(), nullptr, nullptr, &bytes);
    auto upload = UploadBuffer(nullptr, bytes);
    void *mapped;
    D3D12_RANGE none{};
    CheckHR(upload->Map(0, &none, &mapped), "Image upload");
    for (UINT layer = 0; layer < layers.size(); ++layer)
        for (UINT mip = 0; mip < pixels[layer].size(); ++mip)
        {
            auto &p = pixels[layer][mip];
            auto &f = feet[layer * d.MipLevels + mip];
            for (UINT y = 0; y < p.height; ++y)
                std::memcpy(static_cast<uint8_t *>(mapped) + f.Offset + y * f.Footprint.RowPitch,
                            p.rgba.data() + size_t(y) * p.width * 4, p.width * 4);
            timing_.imageBytes += p.rgba.size();
        }
    upload->Unmap(0, nullptr);
    BeginCommands();
    for (UINT i = 0; i < count; ++i)
    {
        D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
        src.pResource = upload.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = feet[i];
        dst.pResource = image.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = i;
        commands_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    auto state = D3D12_RESOURCE_STATE_COPY_DEST;
    Move(image.Get(), state, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    Execute();
    WaitForGpu();
    return image;
}
void Sample::CreateScene()
{
    auto start = std::chrono::steady_clock::now();
    buffers_.clear();
    textures_.clear();
    blas_.clear();
    tlas_.Reset();
    timing_.imageBytes = timing_.bufferBytes = timing_.asBytes = 0;
    timing_.committedImages = timing_.committedBuffers = 0;
    auto upload = [&](std::span<const uint8_t> bytes) {
        timing_.bufferBytes += bytes.size();
        auto result = UploadBuffer(bytes.data(), bytes.size());
        auto desc = result->GetDesc();
        timing_.committedBuffers += device_->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
        return result;
    };
    buffers_.push_back(upload(GalleryBytes(data_.geometries)));
    buffers_.push_back(upload(GalleryBytes(data_.materials)));
    buffers_.push_back(upload(GalleryBytes(data_.instances)));
    if (panel_.binding == 2)
    {
        for (auto g : data_.geometries)
        {
            buffers_.push_back(upload(
                GalleryBytes(data_.vertices).subspan(size_t(g.offsets.x) * 48, size_t(g.lengths.x) * 48)));
            buffers_.push_back(upload(
                GalleryBytes(data_.indices).subspan(size_t(g.offsets.y) * 4, size_t(g.lengths.y) * 4)));
            buffers_.push_back(upload(
                GalleryBytes(data_.meshlets).subspan(size_t(g.offsets.z) * 64, size_t(g.offsets.w) * 64)));
            uint32_t vertices = 0, triangles = 0;
            for (uint32_t i = 0; i < g.offsets.w; ++i)
            {
                auto m = data_.meshlets[g.offsets.z + i];
                vertices = std::max(vertices, m.offsets.x + m.offsets.z);
                triangles = std::max(triangles, m.offsets.y + m.offsets.w * 3);
            }
            buffers_.push_back(upload(
                GalleryBytes(data_.meshletVertices).subspan(size_t(g.lengths.z) * 4, size_t(vertices) * 4)));
            buffers_.push_back(upload(
                GalleryBytes(data_.meshletIndices).subspan(size_t(g.lengths.w) * 4, size_t(triangles) * 4)));
        }
    }
    else
    {
        buffers_.push_back(upload(GalleryBytes(data_.vertices)));
        buffers_.push_back(upload(GalleryBytes(data_.indices)));
        buffers_.push_back(upload(GalleryBytes(data_.meshlets)));
        buffers_.push_back(upload(GalleryBytes(data_.meshletVertices)));
        buffers_.push_back(upload(GalleryBytes(data_.meshletIndices)));
    }
    buffers_.push_back(upload(GalleryBytes(data_.candidates)));
    if (panel_.binding == 0 && GALLERY_TECHNIQUE >= 14)
    {
        for (uint32_t c = 0; c < 4; ++c)
            textures_.push_back(LoadTexture(data_.imageGroups[c], c));
    }
    else
        for (uint32_t i = 0; i < data_.images.size(); ++i)
        {
            auto semantic = data_.images[i].semantic;
            uint32_t channel = semantic == ImageSemantic::Color    ? 0
                               : semantic == ImageSemantic::Normal ? 1
                                                                   : 2;
            textures_.push_back(LoadTexture({i}, channel));
        }
    imageCount_ = UINT(textures_.size());
    timing_.imageResources = imageCount_;
    materialBase_ = imageCount_;
    bufferBase_ = materialBase_ +
                  ((panel_.binding == 0 && GALLERY_TECHNIQUE < 14) ? UINT(data_.materials.size()) * 4 : 0);
    geometryBase_ = bufferBase_ + UINT(buffers_.size());
    uavBase_ = geometryBase_ + ((panel_.binding < 2 && GALLERY_TECHNIQUE == 13) ? 64 * 9 : 0);
    gbufferBase_ = uavBase_ + 6;
    displayBase_ = gbufferBase_ + 4;
    fontSlot_ = displayBase_ + 4;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = fontSlot_ + 1;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CheckHR(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&textureHeap_)), "Gallery descriptor heap");
    descriptorStride_ = device_->GetDescriptorHandleIncrementSize(heap.Type);
    timing_.descriptorCount = heap.NumDescriptors - 1;
    timing_.descriptorBytes = uint64_t(heap.NumDescriptors - 1) * descriptorStride_;
    constants_ = UploadBuffer(nullptr, 512);
    visibility_ = DefaultBuffer(1024 + data_.candidates.size() * 8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    indirect_ = DefaultBuffer(256, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    timing_.sceneBufferResources = buffers_.size();
    timing_.uploadMs = GalleryMs(start);
    if constexpr (GALLERY_TECHNIQUE >= 14)
    {
        start = std::chrono::steady_clock::now();
        BuildAcceleration();
        timing_.asBuildMs = GalleryMs(start);
    }
}
void Sample::CreateSizeResources()
{
    timing_.attachmentCommitted = 0;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    for (UINT i = 0; i < 5; ++i)
    {
        images_[i].Reset();
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = width_;
        d.Height = height_;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = d.Format;
        imageStates_[i] = D3D12_RESOURCE_STATE_COMMON;
        CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, imageStates_[i], &clear,
                                                 IID_PPV_ARGS(&images_[i])),
                "Gallery G-buffer/HDR");
        if (i)
        {
            auto handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += (i + 1) * rtvStride_;
            device_->CreateRenderTargetView(images_[i].Get(), nullptr, handle);
        }
    }
    depth_.Reset();
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = width_;
    d.Height = height_;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_D32_FLOAT;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = d.Format;
    clear.DepthStencil.Depth = 1;
    CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
                                             D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth_)),
            "Gallery depth");
    device_->CreateDepthStencilView(depth_.Get(), nullptr, depthHeap_->GetCPUDescriptorHandleForHeapStart());
    for (const auto &image : images_)
    {
        auto desc = image->GetDesc();
        timing_.attachmentCommitted += device_->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    }
    auto depthDesc = depth_->GetDesc();
    timing_.attachmentCommitted += device_->GetResourceAllocationInfo(0, 1, &depthDesc).SizeInBytes;
    UpdateDescriptors();
}
void Sample::UpdateDescriptors()
{
    auto srv = [&](ID3D12Resource *image, UINT index, bool array = false) {
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
        v.Format = image->GetDesc().Format;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.ViewDimension = array ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D;
        if (array)
        {
            v.Texture2DArray.MipLevels = image->GetDesc().MipLevels;
            v.Texture2DArray.ArraySize = image->GetDesc().DepthOrArraySize;
        }
        else
            v.Texture2D.MipLevels = image->GetDesc().MipLevels;
        device_->CreateShaderResourceView(image, &v, Cpu(index));
    };
    for (UINT i = 0; i < textures_.size(); ++i)
        srv(textures_[i].Get(), i, panel_.binding == 0 && GALLERY_TECHNIQUE >= 14);
    if (panel_.binding == 0 && GALLERY_TECHNIQUE < 14)
        for (UINT m = 0; m < data_.materials.size(); ++m)
            for (UINT c = 0; c < 4; ++c)
                srv(textures_[(&data_.materials[m].images.x)[c]].Get(), materialBase_ + m * 4 + c);
    auto raw = [&](UINT resource, UINT descriptor, UINT64 offset, UINT64 bytes) {
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
        v.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        v.Format = DXGI_FORMAT_R32_TYPELESS;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        v.Buffer.FirstElement = offset / 4;
        v.Buffer.NumElements = UINT(bytes / 4);
        device_->CreateShaderResourceView(buffers_[resource].Get(), &v, Cpu(descriptor));
    };
    for (UINT i = 0; i < buffers_.size(); ++i)
        raw(i, bufferBase_ + i, 0, buffers_[i]->GetDesc().Width);
    if (panel_.binding < 2 && GALLERY_TECHNIQUE == 13)
        for (UINT i = 0; i < data_.geometries.size(); ++i)
        {
            const auto &g = data_.geometries[i];
            UINT refs = 0;
            for (UINT m = 0; m < g.offsets.w; ++m)
                refs += data_.meshlets[g.offsets.z + m].offsets.z;
            UINT64 offsets[] = {0,
                                0,
                                0,
                                UINT64(g.offsets.x) * 48,
                                UINT64(g.offsets.y) * 4,
                                UINT64(g.offsets.z) * 64,
                                UINT64(g.lengths.z) * 4,
                                UINT64(g.lengths.w) * 4,
                                0};
            UINT64 sizes[] = {buffers_[0]->GetDesc().Width,
                              buffers_[1]->GetDesc().Width,
                              buffers_[2]->GetDesc().Width,
                              UINT64(g.lengths.x) * 48,
                              UINT64(g.lengths.y) * 4,
                              UINT64(g.offsets.w) * 64,
                              UINT64(refs) * 4,
                              UINT64(g.lengths.y) * 4,
                              buffers_[8]->GetDesc().Width};
            for (UINT slot = 0; slot < 9; ++slot)
                raw(slot, geometryBase_ + i * 9 + slot, offsets[slot], sizes[slot]);
        }

    for (UINT i = 0; i < 5; ++i)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
        u.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device_->CreateUnorderedAccessView(images_[i].Get(), nullptr, &u, Cpu(uavBase_ + i));
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
    u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    u.Format = DXGI_FORMAT_R32_TYPELESS;
    u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    u.Buffer.NumElements = UINT(visibility_->GetDesc().Width / 4);
    device_->CreateUnorderedAccessView(visibility_.Get(), nullptr, &u, Cpu(uavBase_ + 5));
    for (UINT i = 0; i < 4; ++i)
    {
        srv(images_[i + 1].Get(), gbufferBase_ + i);
        srv(images_[0].Get(), displayBase_ + i);
    }
}
void Sample::CreatePipeline()
{
    raster_.Reset();
    mesh_.Reset();
    display_.Reset();
    light_.Reset();
    reset_.Reset();
    cull_.Reset();
    finalize_.Reset();
    inlinePipeline_.Reset();
    root_.Reset();
    D3D12_DESCRIPTOR_RANGE ranges[] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, panel_.binding == 0 ? 4u : imageCount_, 0, 0, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT(buffers_.size()), 512, 0, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 6, 0, 0, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 900, 0, 0}};
    D3D12_ROOT_PARAMETER p[7]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[1].Constants = {1, 0, 4};
    for (UINT i = 0; i < 4; ++i)
    {
        p[i + 2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p[i + 2].DescriptorTable = {1, &ranges[i]};
    }
    p[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    p[6].Descriptor.ShaderRegister = 904;
    D3D12_STATIC_SAMPLER_DESC s{};
    s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    s.MaxLOD = D3D12_FLOAT32_MAX;
    s.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    auto flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    if (panel_.binding)
        flags =
            D3D12_ROOT_SIGNATURE_FLAGS(flags | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
    D3D12_ROOT_SIGNATURE_DESC root{7, p, 1, &s, flags};
    ComPtr<ID3DBlob> blob, error;
    auto hr = D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (FAILED(hr) && error)
        Trace(std::string(static_cast<char *>(error->GetBufferPointer()), error->GetBufferSize()));
    CheckHR(hr, "Gallery root serialize");
    CheckHR(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                         IID_PPV_ARGS(&root_)),
            "Gallery root");
    auto shader = [&](const wchar_t *name) {
        return ReadBinary(AssetPath(std::filesystem::path(L"Shaders") /
                                    (std::wstring(name) + std::to_wstring(panel_.binding) + L".cso")));
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = root_.Get();
    d.SampleMask = UINT_MAX;
    d.SampleDesc.Count = 1;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.DepthStencilState.DepthEnable = TRUE;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    d.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    d.NumRenderTargets = 4;
    for (UINT i = 0; i < 4; ++i)
    {
        d.RTVFormats[i] = DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    auto vs = shader(L"RasterVertex"), ps = shader(L"RasterPixel");
    d.VS = {vs.data(), vs.size()};
    d.PS = {ps.data(), ps.size()};
    CheckHR(device_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&raster_)), "Gallery raster pipeline");
    if constexpr (GALLERY_TECHNIQUE == 13 || GALLERY_TECHNIQUE == 15)
    {
        struct Stream
        {
            Subobject<ID3D12RootSignature *, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE> root;
            Subobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS> ms;
            Subobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS> ps;
            Subobject<D3D12_RASTERIZER_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER> raster;
            Subobject<D3D12_DEPTH_STENCIL_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL> depth;
            Subobject<D3D12_BLEND_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND> blend;
            Subobject<D3D12_RT_FORMAT_ARRAY, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS>
                targets;
            Subobject<DXGI_FORMAT, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT> format;
            Subobject<DXGI_SAMPLE_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC> sample;
            Subobject<UINT, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK> mask;
            Subobject<D3D12_PRIMITIVE_TOPOLOGY_TYPE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY>
                topology;
        } stream;
        auto ms = shader(L"Mesh");
        stream.root.value = root_.Get();
        stream.ms.value = {ms.data(), ms.size()};
        stream.ps.value = d.PS;
        stream.raster.value = d.RasterizerState;
        stream.depth.value = d.DepthStencilState;
        stream.blend.value = d.BlendState;
        stream.targets.value.NumRenderTargets = 4;
        for (int i = 0; i < 4; ++i)
            stream.targets.value.RTFormats[i] = d.RTVFormats[i];
        stream.format.value = d.DSVFormat;
        stream.sample.value = d.SampleDesc;
        stream.mask.value = UINT_MAX;
        stream.topology.value = d.PrimitiveTopologyType;
        D3D12_PIPELINE_STATE_STREAM_DESC desc{sizeof(stream), &stream};
        CheckHR(device_->CreatePipelineState(&desc, IID_PPV_ARGS(&mesh_)), "Gallery Mesh pipeline");
        D3D12_INDIRECT_ARGUMENT_DESC arg{};
        arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
        D3D12_COMMAND_SIGNATURE_DESC sig{12, 1, &arg, 0};
        CheckHR(device_->CreateCommandSignature(&sig, nullptr, IID_PPV_ARGS(&indirectSignature_)),
                "Indirect Mesh signature");
    }
    vs = shader(L"DisplayVertex");
    ps = shader(L"DisplayPixel");
    d.VS = {vs.data(), vs.size()};
    d.PS = {ps.data(), ps.size()};
    d.NumRenderTargets = 1;
    for (UINT i = 1; i < 8; ++i)
        d.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
    d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    d.DSVFormat = DXGI_FORMAT_UNKNOWN;
    d.DepthStencilState.DepthEnable = FALSE;
    CheckHR(device_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&display_)), "Gallery display pipeline");
    auto compute = [&](ComPtr<ID3D12PipelineState> &out, const wchar_t *name) {
        auto bytes = shader(name);
        D3D12_COMPUTE_PIPELINE_STATE_DESC c{};
        c.pRootSignature = root_.Get();
        c.CS = {bytes.data(), bytes.size()};
        CheckHR(device_->CreateComputePipelineState(&c, IID_PPV_ARGS(&out)), "Gallery compute");
    };
    compute(light_, L"Light");
    compute(countReset_, L"CountReset");
    compute(countVisible_, L"CountVisible");
    if constexpr (GALLERY_TECHNIQUE == 13 || GALLERY_TECHNIQUE == 15)
    {
        compute(reset_, L"Reset");
        compute(cull_, L"Cull");
        compute(finalize_, L"Finalize");
    }
    if constexpr (GALLERY_TECHNIQUE >= 14)
        CreateInlinePipeline();
}
#pragma warning(pop)
void Sample::Bind(bool compute, uint32_t instance)
{
    ID3D12DescriptorHeap *heap[] = {textureHeap_.Get()};
    commands_->SetDescriptorHeaps(1, heap);
    UINT values[] = {instance, 0, UINT(panel_.driven), UINT(data_.candidates.size())};
    UINT image = panel_.binding == 0 && GALLERY_TECHNIQUE < 14
                     ? materialBase_ + data_.instances[instance].ids.y * 4
                     : 0;
    if (compute)
    {
        commands_->SetComputeRootSignature(root_.Get());
        commands_->SetComputeRootConstantBufferView(0, constants_->GetGPUVirtualAddress());
        commands_->SetComputeRoot32BitConstants(1, 4, values, 0);
        commands_->SetComputeRootDescriptorTable(2, Gpu(image));
        commands_->SetComputeRootDescriptorTable(3, Gpu(bufferBase_));
        commands_->SetComputeRootDescriptorTable(4, Gpu(uavBase_));
        commands_->SetComputeRootDescriptorTable(5, Gpu(gbufferBase_));
        if (tlas_)
            commands_->SetComputeRootShaderResourceView(6, tlas_->GetGPUVirtualAddress());
    }
    else
    {
        commands_->SetGraphicsRootSignature(root_.Get());
        commands_->SetGraphicsRootConstantBufferView(0, constants_->GetGPUVirtualAddress());
        commands_->SetGraphicsRoot32BitConstants(1, 4, values, 0);
        commands_->SetGraphicsRootDescriptorTable(2, Gpu(image));
        commands_->SetGraphicsRootDescriptorTable(3, Gpu(bufferBase_));
        commands_->SetGraphicsRootDescriptorTable(4, Gpu(uavBase_));
        commands_->SetGraphicsRootDescriptorTable(5, Gpu(gbufferBase_));
    }
    timing_.binds += 4;
}
void Sample::Stamp(UINT index)
{
    commands_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
}
void Sample::OnUpdate(float seconds)
{
    auto start = std::chrono::steady_clock::now();
    if (!options_.benchmark)
    {
        ImGui_ImplWin32_NewFrame();
        panel_.QueueTestInput();
        ImGui::NewFrame();
        panel_.Draw(data_, "Direct3D12", timing_.cpu[1], timing_.gpu[5]);
        ImGui::Render();
    }
    if (panel_.reset)
    {
        panel_.Initialize(data_, initialOptions_);
        panel_.objects = initialOptions_.galleryObjects;
        panel_.materials = initialOptions_.galleryMaterials;
        panel_.navigation.Reset();
        panel_.reset = false;
    }
    if (panel_.binding != (options_.binding == L"bound"      ? 0
                           : options_.binding == L"textures" ? 1
                                                             : 2) ||
        panel_.objects != data_.objects || panel_.materials != data_.materialCount)
    {
        WaitForGpu();
        DestroyImGui();
        options_.binding = std::array<std::wstring, 3>{L"bound", L"textures", L"resources"}[panel_.binding];
        options_.galleryObjects = panel_.objects;
        options_.galleryMaterials = panel_.materials;
        if (panel_.objects != data_.objects || panel_.materials != data_.materialCount)
            data_ = LoadGallery(options_);
        CreateScene();
        UpdateDescriptors();
        CreatePipeline();
        CreateImGui();
    }
    auto cam = data_.source.cameras.at(panel_.camera);
    if (options_.cameraPath)
        cam = data_.source.path.at(options_.benchmark
                                       ? std::min(timing_.frame < 60 ? 0u : timing_.frame - 60, 1199u)
                                       : options_.pathFrame);
    if (panel_.navigation.Update(window_, seconds, cam.position, cam.target, options_.benchmark))
    {
        cam.position = *panel_.navigation.Position();
        cam.target = *panel_.navigation.Target();
    }
    frame_ = MakeGalleryConstants(data_, cam, width_, height_, panel_.view, panel_.shadows,
                                  panel_.reflections, panel_.culling);
    frame_.heaps.x = 0;
    frame_.heaps.y = bufferBase_;
    frame_.heaps.z = uavBase_;
    void *p;
    D3D12_RANGE none{};
    CheckHR(constants_->Map(0, &none, &p), "Frame map");
    std::memcpy(p, &frame_, sizeof(frame_));
    constants_->Unmap(0, nullptr);
    timing_.cpu[0] = GalleryMs(start);
}
void Sample::OnRender()
{
    auto start = std::chrono::steady_clock::now();
    timing_.draws = timing_.dispatches = timing_.binds = 0;
    BeginCommands();
    Stamp(10);
    Stamp(0);
    if (panel_.driven)
    {
        Bind(true);
        commands_->SetPipelineState(reset_.Get());
        commands_->Dispatch(1, 1, 1);
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        b.UAV.pResource = visibility_.Get();
        commands_->ResourceBarrier(1, &b);
        commands_->SetPipelineState(cull_.Get());
        commands_->Dispatch((UINT(data_.candidates.size()) + 63) / 64, 1, 1);
        commands_->ResourceBarrier(1, &b);
        commands_->SetPipelineState(finalize_.Get());
        commands_->Dispatch(1, 1, 1);
        commands_->ResourceBarrier(1, &b);
        auto vis = D3D12_RESOURCE_STATE_UNORDERED_ACCESS, args = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        Move(visibility_.Get(), vis, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Move(indirect_.Get(), args, D3D12_RESOURCE_STATE_COPY_DEST);
        commands_->CopyBufferRegion(indirect_.Get(), 0, visibility_.Get(), 16, 12);
        Move(visibility_.Get(), vis, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Move(indirect_.Get(), args, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        timing_.dispatches += 3;
    }
    Stamp(1);
    Stamp(2);
    D3D12_VIEWPORT viewport{0, 0, float(width_), float(height_), 0, 1};
    D3D12_RECT rect{0, 0, LONG(width_), LONG(height_)};
    commands_->RSSetViewports(1, &viewport);
    commands_->RSSetScissorRects(1, &rect);
    if constexpr (GALLERY_TECHNIQUE != 14)
    {
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 4> rt{};
        float zero[4]{};
        for (UINT i = 0; i < 4; ++i)
        {
            Move(images_[i + 1].Get(), imageStates_[i + 1], D3D12_RESOURCE_STATE_RENDER_TARGET);
            rt[i] = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
            rt[i].ptr += (i + 2) * rtvStride_;
            commands_->ClearRenderTargetView(rt[i], zero, 0, nullptr);
        }
        auto depth = depthHeap_->GetCPUDescriptorHandleForHeapStart();
        commands_->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
        commands_->OMSetRenderTargets(4, rt.data(), FALSE, &depth);
        if (panel_.driven)
        {
            Bind(false);
            commands_->SetPipelineState(mesh_.Get());
            commands_->ExecuteIndirect(indirectSignature_.Get(), 1, indirect_.Get(), 0, nullptr, 0);
            ++timing_.dispatches;
        }
        else
        {
            Bind(false, 0);
            UINT lastMaterial = data_.instances[0].ids.y, lastGeometry = UINT_MAX;
            for (UINT i = 0; i < data_.instances.size(); ++i)
            {
                auto g = data_.geometries[data_.instances[i].ids.x];
                if (panel_.binding < 2 && GALLERY_TECHNIQUE == 13 && lastGeometry != data_.instances[i].ids.x)
                {
                    lastGeometry = data_.instances[i].ids.x;
                    commands_->SetGraphicsRootDescriptorTable(3, Gpu(geometryBase_ + lastGeometry * 9));
                    ++timing_.binds;
                }
                UINT push[] = {i, 0, 0, UINT(data_.candidates.size())};
                commands_->SetGraphicsRoot32BitConstants(1, 4, push, 0);
                if (panel_.binding == 0 && GALLERY_TECHNIQUE < 14 && data_.instances[i].ids.y != lastMaterial)
                {
                    commands_->SetGraphicsRootDescriptorTable(
                        2, Gpu(materialBase_ + data_.instances[i].ids.y * 4));
                    lastMaterial = data_.instances[i].ids.y;
                    ++timing_.binds;
                }
                if constexpr (GALLERY_TECHNIQUE == 12)
                {
                    commands_->SetPipelineState(raster_.Get());
                    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    D3D12_INDEX_BUFFER_VIEW ib{buffers_[4]->GetGPUVirtualAddress() +
                                                   uint64_t(g.offsets.y) * 4,
                                               g.lengths.y * 4, DXGI_FORMAT_R32_UINT};
                    commands_->IASetIndexBuffer(&ib);
                    commands_->DrawIndexedInstanced(g.lengths.y, 1, 0, 0, 0);
                    ++timing_.draws;
                }
                else
                {
                    commands_->SetPipelineState(mesh_.Get());
                    commands_->DispatchMesh(g.offsets.w, 1, 1);
                    ++timing_.dispatches;
                }
            }
        }
        for (UINT i = 1; i < 5; ++i)
            Move(images_[i].Get(), imageStates_[i], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    }
    Stamp(3);
    Stamp(4);
    Move(images_[0].Get(), imageStates_[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if constexpr (GALLERY_TECHNIQUE == 14)
        Move(images_[1].Get(), imageStates_[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if constexpr (GALLERY_TECHNIQUE >= 14)
    {
        Bind(true);
        commands_->SetPipelineState(inlinePipeline_.Get());
        commands_->Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
        ++timing_.dispatches;
    }
    Stamp(5);
    Stamp(6);
    if constexpr (GALLERY_TECHNIQUE < 14)
    {
        Bind(true);
        commands_->SetPipelineState(light_.Get());
        commands_->Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
        ++timing_.dispatches;
    }
    Stamp(7);
    Move(images_[0].Get(), imageStates_[0], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    Stamp(8);
    renderedBuffer_ = swapchain_->GetCurrentBackBufferIndex();
    auto state = D3D12_RESOURCE_STATE_PRESENT;
    Move(targets_[renderedBuffer_].Get(), state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rt = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rt.ptr += renderedBuffer_ * rtvStride_;
    commands_->OMSetRenderTargets(1, &rt, FALSE, nullptr);
    Bind(false);
    commands_->SetGraphicsRootDescriptorTable(5, Gpu(displayBase_));
    ++timing_.binds;
    commands_->SetPipelineState(display_.Get());
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands_->DrawInstanced(3, 1, 0, 0);
    ++timing_.draws;
    Stamp(9);
    Stamp(11);
    if (!options_.benchmark && panel_.visible)
        RenderImGui();
    Move(targets_[renderedBuffer_].Get(), state, D3D12_RESOURCE_STATE_PRESENT);
    commands_->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 12,
                                timestampReadback_.Get(), 0);
    CheckHR(commands_->Close(), "Frame close");
    timing_.cpu[1] = GalleryMs(start);
    start = std::chrono::steady_clock::now();
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    timing_.cpu[2] = GalleryMs(start);
    start = std::chrono::steady_clock::now();
    CheckHR(swapchain_->Present(options_.benchmark ? 0 : 1, 0), "Present");
    timing_.cpu[4] = GalleryMs(start);
    start = std::chrono::steady_clock::now();
    WaitForGpu();
    timing_.cpu[3] = GalleryMs(start);
    void *p;
    D3D12_RANGE range{0, 96};
    CheckHR(timestampReadback_->Map(0, &range, &p), "Timestamp readback");
    auto ticks = static_cast<const uint64_t *>(p);
    for (UINT i = 0; i < 6; ++i)
        timing_.gpu[i] = double(ticks[i * 2 + 1] - ticks[i * 2]) * 1000 / timestampFrequency_;
    timestampReadback_->Unmap(0, nullptr);
    timing_.Record(options_.benchmark);
    if (auto capture = panel_.TestCapture(); !capture.empty())
        OnScreenshot(capture);
    ReadValidationMessages();
}
void Sample::OnResize(uint32_t width, uint32_t height)
{
    if (!width || !height)
        return;
    WaitForGpu();
    for (auto &r : targets_)
        r.Reset();
    CheckHR(swapchain_->ResizeBuffers(2, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
    width_ = width;
    height_ = height;
    CreateTargets();
    CreateSizeResources();
}
bool Sample::OnMessage(HWND window, UINT message, WPARAM key, LPARAM point, LRESULT &result)
{
    if (!ImGui::GetCurrentContext())
        return false;
    bool handled = ImGui_ImplWin32_WndProcHandler(window, message, key, point) != 0;
    handled = panel_.navigation.Message(window, message, key, point) || handled;
    result = 0;
    return handled;
}
void Sample::ReadValidationMessages()
{
    if (!infoQueue_)
        return;
    UINT64 count = infoQueue_->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (UINT64 i = 0; i < count; ++i)
    {
        SIZE_T bytes = 0;
        infoQueue_->GetMessage(i, nullptr, &bytes);
        std::vector<uint8_t> storage(bytes);
        auto message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
        CheckHR(infoQueue_->GetMessage(i, message, &bytes), "Debug message");
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
            ValidationError(message->pDescription);
    }
    infoQueue_->ClearStoredMessages();
}
void Sample::OnDestroy() noexcept
{
    try
    {
        if (queue_ && fence_ && fenceEvent_)
            WaitForGpu();
        if (timing_.frame && (!options_.timings.empty() || options_.captureGpu))
            CollectStatistics();
        timing_.Save(options_, data_, panel_);
        DestroyImGui();
        ReadValidationMessages();
    }
    catch (const std::exception &e)
    {
        ValidationError(e.what());
    }
    if (fenceEvent_)
        CloseHandle(fenceEvent_);
    fenceEvent_ = nullptr;
}

void Sample::BuildAcceleration()
{
    for (const auto &g : data_.geometries)
    {
        auto vertices = buffers_[panel_.binding == 2 ? g.resources.x : 3],
             indices = buffers_[panel_.binding == 2 ? g.resources.y : 4];
        D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
        geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        // Material overrides may make a shared geometry MASK. Any-hit performs the test.
        auto &t = geometry.Triangles;
        t.VertexBuffer = {
            vertices->GetGPUVirtualAddress() + (panel_.binding == 2 ? 0 : uint64_t(g.offsets.x) * 48), 48};
        t.VertexCount = g.lengths.x;
        t.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        t.IndexBuffer =
            indices->GetGPUVirtualAddress() + (panel_.binding == 2 ? 0 : uint64_t(g.offsets.y) * 4);
        t.IndexCount = g.lengths.y;
        t.IndexFormat = DXGI_FORMAT_R32_UINT;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.NumDescs = 1;
        inputs.pGeometryDescs = &geometry;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
        device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        if (!info.ResultDataMaxSizeInBytes)
            throw std::runtime_error("BLAS prebuild failed");
        auto result = DefaultBuffer(info.ResultDataMaxSizeInBytes,
                                    D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE),
             scratch = DefaultBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        BeginCommands();
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
        build.Inputs = inputs;
        build.DestAccelerationStructureData = result->GetGPUVirtualAddress();
        build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
        commands_->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = result.Get();
        commands_->ResourceBarrier(1, &barrier);
        Execute();
        WaitForGpu();
        blas_.push_back(result);
        timing_.asBytes += info.ResultDataMaxSizeInBytes;
    }
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances;
    for (UINT i = 0; i < data_.instances.size(); ++i)
    {
        const auto &instance = data_.instances[i];
        D3D12_RAYTRACING_INSTANCE_DESC gpu{};
        XMFLOAT4X4 transpose;
        XMStoreFloat4x4(&transpose, XMMatrixTranspose(XMLoadFloat4x4(&instance.world)));
        std::memcpy(gpu.Transform, &transpose, 48);
        gpu.InstanceID = i;
        gpu.InstanceMask = 255;
        gpu.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        gpu.AccelerationStructure = blas_[instance.ids.x]->GetGPUVirtualAddress();
        instances.push_back(gpu);
    }
    auto instanceBuffer = UploadBuffer(instances.data(), instances.size() * sizeof(instances[0]));
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = UINT(instances.size());
    inputs.InstanceDescs = instanceBuffer->GetGPUVirtualAddress();
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    tlas_ =
        DefaultBuffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    auto scratch = DefaultBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    BeginCommands();
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs = inputs;
    build.DestAccelerationStructureData = tlas_->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
    commands_->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = tlas_.Get();
    commands_->ResourceBarrier(1, &barrier);
    Execute();
    WaitForGpu();
    timing_.asBytes += info.ResultDataMaxSizeInBytes;
}
void Sample::OnScreenshot(const std::filesystem::path &path)
{
    auto *image = targets_[renderedBuffer_].Get();
    auto desc = image->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT foot{};
    UINT64 bytes;
    device_->GetCopyableFootprints(&desc, 0, 1, 0, &foot, nullptr, nullptr, &bytes);
    auto readback = DefaultBuffer(bytes, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_HEAP_TYPE_READBACK);
    BeginCommands();
    auto state = D3D12_RESOURCE_STATE_PRESENT;
    Move(image, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = image;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = foot;
    commands_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Move(image, state, D3D12_RESOURCE_STATE_PRESENT);
    Execute();
    WaitForGpu();
    void *p;
    D3D12_RANGE range{0, SIZE_T(bytes)};
    CheckHR(readback->Map(0, &range, &p), "Screenshot map");
    SavePng(path, width_, height_, static_cast<uint8_t *>(p) + foot.Offset, foot.Footprint.RowPitch);
    readback->Unmap(0, nullptr);
    if (options_.captureGpu)
        DumpGpuData(path);
    ReadValidationMessages();
}
void Sample::DumpGpuData(const std::filesystem::path &path)
{
    auto directory = path.parent_path() / (path.stem().wstring() + L"-gpu");
    std::filesystem::create_directories(directory);
    const char *names[] = {
        "hdr", GALLERY_TECHNIQUE == 14 ? "surface" : "base", "normal", "position", "emission", "depth"};
    for (UINT i = 0; i < (GALLERY_TECHNIQUE == 14 ? 2u : 6u); ++i)
    {
        auto target = i == 5 ? depth_.Get() : images_[i].Get();
        auto depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        auto &state = i == 5 ? depthState : imageStates_[i];
        auto desc = target->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT foot{};
        UINT64 bytes;
        device_->GetCopyableFootprints(&desc, 0, 1, 0, &foot, nullptr, nullptr, &bytes);
        auto readback = DefaultBuffer(bytes, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_HEAP_TYPE_READBACK);
        BeginCommands();
        auto before = state;
        Move(target, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
        src.pResource = target;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = foot;
        commands_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        Move(target, state, before);
        Execute();
        WaitForGpu();
        void *p;
        D3D12_RANGE range{0, SIZE_T(bytes)};
        CheckHR(readback->Map(0, &range, &p), "HDR readback");
        std::ofstream file(directory / (std::string(names[i]) + ".bin"), std::ios::binary);
        for (UINT y = 0; y < height_; ++y)
            file.write(reinterpret_cast<const char *>(p) + foot.Offset + y * foot.Footprint.RowPitch,
                       std::streamsize(width_) * (i == 5 ? 4 : 16));
        readback->Unmap(0, nullptr);
    }
    nlohmann::json metadata = {{"width", width_},           {"height", height_},
                               {"format", "float32 RGBA"},  {"objects", data_.instances.size()},
                               {"binding", panel_.binding}, {"geometryShared", true},
                               {"frame", timing_.frame}};
    metadata["depthFormat"] = GALLERY_TECHNIQUE == 14 ? "surface.z = ray distance" : "depth.bin = D32 float";
    for (const auto &instance : data_.instances)
    {
        const auto *matrix = &instance.world._11;
        metadata["instances"].push_back({{"geometry", instance.ids.x},
                                         {"material", instance.ids.y},
                                         {"world", std::vector<float>(matrix, matrix + 16)}});
    }
    std::ofstream(directory / "metadata.json") << metadata.dump(2);
}

void Sample::CollectStatistics()
{
    const auto savedBinds = timing_.binds;
    auto readback = DefaultBuffer(516, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_HEAP_TYPE_READBACK);
    BeginCommands();
    Bind(true);
    D3D12_RESOURCE_BARRIER uav{};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    if constexpr (GALLERY_TECHNIQUE == 14)
    {
        uav.UAV.pResource = images_[1].Get();
        commands_->ResourceBarrier(1, &uav);
    }
    uav.UAV.pResource = visibility_.Get();
    commands_->SetPipelineState(countReset_.Get());
    commands_->Dispatch(1, 1, 1);
    commands_->ResourceBarrier(1, &uav);
    commands_->SetPipelineState(countVisible_.Get());
    commands_->Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
    commands_->ResourceBarrier(1, &uav);
    auto state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Move(visibility_.Get(), state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands_->CopyBufferRegion(readback.Get(), 0, visibility_.Get(), 32, 512);
    commands_->CopyBufferRegion(readback.Get(), 512, visibility_.Get(), 0, 4);
    Move(visibility_.Get(), state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Execute();
    WaitForGpu();
    void *mapped;
    D3D12_RANGE range{0, 516};
    CheckHR(readback->Map(0, &range, &mapped), "Visibility statistics");
    const auto *words = static_cast<const uint32_t *>(mapped);
    std::set<uint32_t> geometries;
    timing_.visibleInstances = 0;
    for (uint32_t i = 0; i < data_.instances.size(); ++i)
        if (words[i / 32] & (1u << (i % 32)))
        {
            ++timing_.visibleInstances;
            geometries.insert(data_.instances[i].ids.x);
        }
    timing_.visibleGeometries = uint32_t(geometries.size());
    timing_.gpuVisibleMeshlets = panel_.driven ? words[128] : 0;
    readback->Unmap(0, nullptr);
    auto reference = GalleryFrustumCandidates(data_, frame_);
    timing_.frustumInstances = reference[0];
    timing_.frustumMeshlets = reference[1];
    timing_.binds = savedBinds;
}

void Sample::CreateInlinePipeline()
{
    auto bytes = ReadBinary(AssetPath(std::filesystem::path(L"Shaders") /
                                      (L"InlineHybrid" + std::to_wstring(panel_.binding) + L".cso")));
    D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
    d.pRootSignature = root_.Get();
    d.CS = {bytes.data(), bytes.size()};
    CheckHR(device_->CreateComputePipelineState(&d, IID_PPV_ARGS(&inlinePipeline_)),
            "Inline Hybrid CS pipeline");
}
