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
    options_ = options;
    workshop_ = LoadBSDFScene(options.scene.empty() ? AssetPath(L"Assets/Scenes/BSDFMaterials/BSDFMaterials.json") : options.scene);
    workshop_ = BSDFFixture(std::move(workshop_), options.bsdfTest);
    panel_.Initialize(workshop_, options);
    window_ = window;
    width_ = width;
    height_ = height;
    havePrevious_ = false;
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
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 rt{};
    CheckHR(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &rt, sizeof(rt)), "DXR feature query");
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
    return cameraHandled || result != 0 || (io.WantCaptureKeyboard &&
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
    rayData_ = MakeBSDFSceneData(workshop_);
    rayVertices_ = UploadBuffer(rayData_.vertices.data(), rayData_.vertices.size() * sizeof(RayVertex));
    rayIndices_ = UploadBuffer(rayData_.indices.data(), rayData_.indices.size() * 4);
    rayPrimitives_ =
        UploadBuffer(rayData_.primitives.data(), rayData_.primitives.size() * sizeof(RayPrimitive));
    rayMaterials_ = UploadBuffer(rayData_.materials.data(), rayData_.materials.size() * sizeof(BSDFMaterial));
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
    BuildBlas();
    BuildTlas();
}
Sample::ComPtr<ID3D12Resource> Sample::DefaultBuffer(uint64_t size, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        state = D3D12_RESOURCE_STATE_COMMON;
    auto desc = BufferDescription(size);
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> r;
    CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                             IID_PPV_ARGS(&r)),
            "AS buffer");
    return r;
}
void Sample::BuildBlas()
{
    blas_.resize(rayData_.primitives.size());
    for (size_t i = 0; i < rayData_.primitives.size(); ++i)
    {
        const auto &p = rayData_.primitives[i].offsets;
        const auto &primitive = workshop_.primitives[i];
        if (workshop_.materials[p.z].alpha == AlphaMode::Blend)
            continue;
        D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
        geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometry.Flags = workshop_.materials[p.z].alpha == AlphaMode::Opaque
                             ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
                             : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        auto &t = geometry.Triangles;
        t.VertexBuffer = {rayVertices_->GetGPUVirtualAddress() + uint64_t(p.x) * sizeof(RayVertex),
                          sizeof(RayVertex)};
        t.VertexCount = UINT(primitive.vertices.size());
        t.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        t.IndexBuffer = rayIndices_->GetGPUVirtualAddress() + uint64_t(p.y) * 4;
        t.IndexCount = p.w;
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
        blas_[i] = DefaultBuffer(info.ResultDataMaxSizeInBytes,
                                 D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        auto scratch = DefaultBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        CheckHR(allocator_->Reset(), "BLAS allocator");
        CheckHR(commands_->Reset(allocator_.Get(), nullptr), "BLAS commands");
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
        build.Inputs = inputs;
        build.DestAccelerationStructureData = blas_[i]->GetGPUVirtualAddress();
        build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
        commands_->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = blas_[i].Get();
        commands_->ResourceBarrier(1, &barrier);
        CheckHR(commands_->Close(), "BLAS close");
        ID3D12CommandList *lists[] = {commands_.Get()};
        queue_->ExecuteCommandLists(1, lists);
        WaitForGpu();
    }
    Trace("Shared BLAS: " +
          std::to_string(std::count_if(blas_.begin(), blas_.end(), [](auto &b) { return bool(b); })));
}
void Sample::BuildTlas()
{
    WaitForGpu();
    rayInstances_ = MakeRayInstances(workshop_, panel_.scene, panel_.preset, panel_.occluder);
    havePrevious_ = false;
    pathLights_ = MakePathLights(workshop_, rayInstances_);
    areaLights_ = UploadBuffer(pathLights_.lights.data(), pathLights_.lights.size() * sizeof(PathLight));

    rayInstancesBuffer_ = UploadBuffer(rayInstances_.data(), rayInstances_.size() * sizeof(RayInstance));
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances(rayInstances_.size());
    for (size_t i = 0; i < instances.size(); ++i)
    {
        const auto &r = rayInstances_[i];
        auto &d = instances[i];
        const auto *m = &r.world.m[0][0];
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 4; ++col)
                d.Transform[row][col] = m[col * 4 + row];
        d.InstanceID = UINT(i);
        d.InstanceMask = 255;
        d.AccelerationStructure = blas_[r.primitive.x]->GetGPUVirtualAddress();
        const auto &material = workshop_.materials[workshop_.primitives[r.primitive.x].material];
        d.Flags = (material.doubleSided || material.transmission > 0) ? D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE
                                       : D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    }
    auto upload = UploadBuffer(instances.data(), instances.size() * sizeof(instances[0]));
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = UINT(instances.size());
    inputs.InstanceDescs = upload->GetGPUVirtualAddress();
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    tlas_ =
        DefaultBuffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    auto scratch = DefaultBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    CheckHR(allocator_->Reset(), "TLAS allocator");
    CheckHR(commands_->Reset(allocator_.Get(), nullptr), "TLAS commands");
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs = inputs;
    build.DestAccelerationStructureData = tlas_->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
    commands_->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = tlas_.Get();
    commands_->ResourceBarrier(1, &barrier);
    CheckHR(commands_->Close(), "TLAS close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitForGpu();
    builtScene_ = panel_.scene;
    builtPreset_ = panel_.preset;
    builtOccluder_ = panel_.occluder;
    Trace("TLAS instances: " + std::to_string(instances.size()));
}
void Sample::CreateSizeResources()
{
    auto make = [&](UINT w, UINT h, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                    D3D12_RESOURCE_STATES state, bool depth)
    {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{depth ? DXGI_FORMAT_D32_FLOAT : format};
        CheckHR(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)),
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
                       i ? std::max(1u, (height_ + 1) / 2) : height_, (i ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT),
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
                          sizeof(BSDFMaterial),  sizeof(WorkshopLight)};
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
    for (UINT i = 0; i < 3; ++i)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC v{};
        v.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        v.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        device_->CreateUnorderedAccessView(i ? diagnostic_[i - 1].Get() : hdr_[0].Get(), nullptr, &v, rtCpu);
        rtCpu.ptr += stride;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC lightView{};
    lightView.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    lightView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    lightView.Buffer.NumElements = UINT(pathLights_.lights.size());
    lightView.Buffer.StructureByteStride = sizeof(PathLight);
    device_->CreateShaderResourceView(areaLights_.Get(), &lightView, rtCpu);
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
        u.Format = p < 3 ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
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
    CreateRayPipeline();
}
void Sample::CreateRayPipeline()
{
    D3D12_DESCRIPTOR_RANGE ranges[3] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 75, 0, 0, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, 75},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 75, 0, 78}};
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable = {3, ranges};
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
    subobjects.reserve(12);
    for (UINT i = 0; i < 5u; ++i)
    {
        libraries[i] =
            ReadBinary(AssetPath(std::filesystem::path(L"Shaders") / (std::wstring(names[i]) + L".cso")));
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
    for (UINT i = 0; i < 2u; ++i)
        subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hits[i]});
    D3D12_RAYTRACING_SHADER_CONFIG config{24, 8};
    subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &config});
    auto *globalRoot = rootSignature_.Get();
    subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &globalRoot});
    D3D12_RAYTRACING_PIPELINE_CONFIG recursion{1};
    subobjects.push_back({D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &recursion});
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
    if (auto p = panel_.TestCapture(); !p.empty())
        OnScreenshot(p);
    ImGui_ImplWin32_NewFrame();
    panel_.QueueTestInput();
    ImGui::NewFrame();
    panel_.Draw(workshop_, "Direct3D12");
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
    if (builtScene_ != panel_.scene || builtPreset_ != panel_.preset || builtOccluder_ != panel_.occluder)
        BuildTlas();
    UpdateDescriptors();
    if (panel_.materialsDirty) {
        rayData_ = MakeBSDFSceneData(workshop_);
        void *mapped = nullptr; D3D12_RANGE noRead{};
        CheckHR(rayMaterials_->Map(0, &noRead, &mapped), "BSDF material map");
        std::memcpy(mapped, rayData_.materials.data(), rayData_.materials.size() * sizeof(BSDFMaterial));
        rayMaterials_->Unmap(0, nullptr); panel_.materialsDirty = false;
    }
    auto c = panel_.Constants(workshop_, width_, height_, 0, pathLights_.count);
    if (!havePrevious_ || panel_.resetRequested || std::memcmp(&c, previousConstants_.data(), sizeof(c)) != 0) {
        samples_ = 0; panel_.resetRequested = false;
        std::memcpy(previousConstants_.data(), &c, sizeof(c)); havePrevious_ = true;
        Trace("Path accumulation reset");
    }
    c.controls.z = samples_;
    const bool traceSample = (!panel_.paused || samples_ == 0) && (!panel_.targetSpp || samples_ < panel_.targetSpp);
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
        p.flags = {0, 0, 0, 0};
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
    dispatch.MissShaderTable = {address + 64, 64u, 32};
    dispatch.HitGroupTable = {address + 128, 64u, 32};
    dispatch.Width = width_;
    dispatch.Height = height_;
    dispatch.Depth = 1;
    if (traceSample) { commands_->DispatchRays(&dispatch); ++samples_; }
    panel_.samples = samples_;
    for (UINT i = 0; i < 2; ++i)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = diagnostic_[i].Get();
        commands_->ResourceBarrier(1, &barrier);
    }
    Move(hdr_[0].Get(), hdrStates_[0], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    for (UINT i = 1; i < 4; ++i)
        Move(hdr_[i].Get(), hdrStates_[i], D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
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
    CheckHR(commands_->Close(), "Frame close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    CheckHR(swapchain_->Present(1, 0), "Present");
    WaitForGpu();
    ReadValidationMessages();
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
    havePrevious_ = false;
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
                               {"technique", 19}, {"format", "rgba32f"},
                               {"scene", workshop_.scenes[panel_.scene].name},
                               {"instances", rayInstances_.size()},
                               {"verticesShared", true}, {"fixture", std::filesystem::path(options_.bsdfTest).string()}, {"mode", panel_.ModeName()}, {"preset", panel_.scene}, {"initialMedium", panel_.initialMedium}, {"materialRevision", panel_.revision}, {"spp", samples_}, {"seed", panel_.seed}, {"areaLights", pathLights_.count}, {"sampling", panel_.sampling ? "mis" : "bsdf"}, {"bounces", panel_.directOnly ? 1 : panel_.bounces}};
    metadata["cameraName"] = workshop_.cameras[panel_.camera].name;
    metadata["materials"] = nlohmann::json::array();
    for(const auto &m:workshop_.materials) if(m.transmission>0) metadata["materials"].push_back({
        {"id",m.id},{"ior",m.ior},{"roughness",m.factors.y},{"transmission",m.transmission},
        {"attenuationColor",{m.attenuationColor.x,m.attenuationColor.y,m.attenuationColor.z}},{"attenuationDistance",m.attenuationDistance}});
    const char *names[] = {"hdr", "surface", "sample"};
    const UINT sizes[] = {16, 16, 16};
    for (UINT i = 0; i < 3; ++i)
    {
        auto image = i == 0 ? hdr_[0].Get() : diagnostic_[i - 1].Get();
        auto &state = i == 0 ? hdrStates_[0] : diagnosticStates_[i - 1];
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
    areaLights_.Reset();
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
