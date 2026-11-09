#include "Sample.h"
#include <cstring>
using namespace tutorial;
namespace
{
D3D12_RESOURCE_DESC CullBuffer(uint64_t bytes, bool uav = false)
{
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (uav)
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return d;
}
} // namespace
void Sample::CreateCullingSizeResources()
{
    auto make = [&](uint64_t bytes, D3D12_HEAP_TYPE type, bool uav, D3D12_RESOURCE_STATES state)
    {
        ComPtr<ID3D12Resource> r;
        D3D12_HEAP_PROPERTIES h{};
        h.Type = type;
        auto d = CullBuffer(bytes, uav);
        if (type == D3D12_HEAP_TYPE_DEFAULT)
            state = D3D12_RESOURCE_STATE_COMMON;
        CheckHR(
            device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r)),
            "Culling buffer");
        return r;
    };
    if (!cullRoot_)
    {
        D3D12_DESCRIPTOR_RANGE ranges[] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0},
                                           {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 0}};
        D3D12_ROOT_PARAMETER p[3]{};
        p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        p[0].Descriptor.ShaderRegister = 0;
        for (int i = 0; i < 2; ++i)
        {
            p[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p[i + 1].DescriptorTable = {1, &ranges[i]};
        }
        D3D12_ROOT_SIGNATURE_DESC d{3, p, 0, nullptr,
                                    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        ComPtr<ID3DBlob> bytes, error;
        CheckHR(D3D12SerializeRootSignature(&d, D3D_ROOT_SIGNATURE_VERSION_1, &bytes, &error),
                "Culling root serialization");
        CheckHR(device_->CreateRootSignature(0, bytes->GetBufferPointer(), bytes->GetBufferSize(),
                                             IID_PPV_ARGS(&cullRoot_)),
                "Culling root");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 5;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        CheckHR(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&cullHeap_)), "Culling descriptors");
        cullBounds_ = UploadBuffer(nullptr, 4600 * sizeof(CullBounds));
        cullConstants_ = UploadBuffer(nullptr, 16 * 256);
        cullPredicates_ = make(4600 * 8, D3D12_HEAP_TYPE_DEFAULT, true, D3D12_RESOURCE_STATE_COPY_DEST);
        cullReadback_ = make(4600 * 8, D3D12_HEAP_TYPE_READBACK, false, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_QUERY_HEAP_DESC q{};
        q.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
        q.Count = 4600;
        CheckHR(device_->CreateQueryHeap(&q, IID_PPV_ARGS(&occlusionQueries_)), "Occlusion query heap");
    }
    paddedWidth_ = CullPowerOfTwo(width_);
    paddedHeight_ = CullPowerOfTwo(height_);
    pyramidCount_ = 0;
    for (UINT w = paddedWidth_, h = paddedHeight_;; w = std::max(1u, w / 2), h = std::max(1u, h / 2))
    {
        pyramidCount_ += w * h;
        if (w == 1 && h == 1)
            break;
    }
    cullPyramid_ = make(uint64_t(pyramidCount_) * 4, D3D12_HEAP_TYPE_DEFAULT, true,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    pyramidState_ = D3D12_RESOURCE_STATE_COMMON;
    softwareUpload_ = UploadBuffer(nullptr, uint64_t(width_) * height_ * 4);
    auto handle = cullHeap_->GetCPUDescriptorHandleForHeapStart();
    UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(gbuffer_[4].Get(), &srv, handle);
    handle.ptr += stride;
    srv = {};
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = 4600;
    srv.Buffer.StructureByteStride = sizeof(CullBounds);
    device_->CreateShaderResourceView(cullBounds_.Get(), &srv, handle);
    handle.ptr += stride;
    srv.Buffer.NumElements = CULL_SAMPLE == 33 ? width_ * height_ : pyramidCount_;
    srv.Buffer.StructureByteStride = 4;
    device_->CreateShaderResourceView(CULL_SAMPLE == 33 ? softwareUpload_.Get() : cullPyramid_.Get(), &srv,
                                      handle);
    handle.ptr += stride;
    D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
    u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    u.Buffer.NumElements = pyramidCount_;
    u.Buffer.StructureByteStride = 4;
    device_->CreateUnorderedAccessView(cullPyramid_.Get(), nullptr, &u, handle);
    handle.ptr += stride;
    u.Format = DXGI_FORMAT_R32_TYPELESS;
    u.Buffer.NumElements = 4600 * 2;
    u.Buffer.StructureByteStride = 0;
    u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device_->CreateUnorderedAccessView(cullPredicates_.Get(), nullptr, &u, handle);
}
void Sample::BindCulling(UINT slot, const CullConstants &value, bool compute)
{
    void *mapped = nullptr;
    D3D12_RANGE empty{};
    CheckHR(cullConstants_->Map(0, &empty, &mapped), "Cull constants map");
    std::memcpy(static_cast<uint8_t *>(mapped) + slot * 256, &value, sizeof(value));
    cullConstants_->Unmap(0, nullptr);
    ID3D12DescriptorHeap *heap[] = {cullHeap_.Get()};
    commands_->SetDescriptorHeaps(1, heap);
    auto srv = cullHeap_->GetGPUDescriptorHandleForHeapStart(), uav = srv;
    uav.ptr += 3 * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    if (compute)
    {
        commands_->SetComputeRootSignature(cullRoot_.Get());
        commands_->SetComputeRootConstantBufferView(0, cullConstants_->GetGPUVirtualAddress() + slot * 256);
        commands_->SetComputeRootDescriptorTable(1, srv);
        commands_->SetComputeRootDescriptorTable(2, uav);
    }
    else
    {
        commands_->SetGraphicsRootSignature(cullRoot_.Get());
        commands_->SetGraphicsRootConstantBufferView(0, cullConstants_->GetGPUVirtualAddress() + slot * 256);
        commands_->SetGraphicsRootDescriptorTable(1, srv);
    }
}
void Sample::CullingPass()
{
    auto &c = panel_.culling;
    auto dsv = sceneDsv_->GetCPUDescriptorHandleForHeapStart();
    querySlots_.assign(c.total, -1);
    queryCount_ = 0;
    CullConstants constants{};
    constants.viewProjection = c.viewProjection;
    constants.dimensions = {width_, height_, paddedWidth_, paddedHeight_};
    constants.info.x = c.total;
    void *mapped = nullptr;
    D3D12_RANGE empty{};
    CheckHR(cullBounds_->Map(0, &empty, &mapped), "Cull bounds map");
    std::memcpy(mapped, c.bounds.data(), c.bounds.size() * sizeof(CullBounds));
    cullBounds_->Unmap(0, nullptr);
    if (c.mode == CullMode::Software && c.diagnostic == 2)
    {
        CheckHR(softwareUpload_->Map(0, &empty, &mapped), "Software depth map");
        std::memcpy(mapped, c.software.pixels.data(), c.software.pixels.size() * 4);
        softwareUpload_->Unmap(0, nullptr);
    }
    Stamp(20);
    if (c.UsesDepth())
    {
        commands_->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
        DrawObjects(4);
    }
    Stamp(21);
    Stamp(22);
    if (c.mode == CullMode::Query)
    {
        commands_->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
        commands_->SetPipelineState(boundsPipeline_.Get());
        commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for (UINT i = 0; i < c.total; ++i)
            if (c.Candidate(i) && !c.Occluder(i) && c.projected[i].safe)
            {
                UINT slot = queryCount_++;
                querySlots_[i] = int(slot);
                Bind(i + 4601, 0);
                commands_->BeginQuery(occlusionQueries_.Get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, slot);
                commands_->DrawInstanced(36, 1, 0, 0);
                commands_->EndQuery(occlusionQueries_.Get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, slot);
            }
        if (queryCount_)
        {
            Move(cullPredicates_.Get(), predicateState_, D3D12_RESOURCE_STATE_COPY_DEST);
            commands_->ResolveQueryData(occlusionQueries_.Get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0,
                                        queryCount_, cullPredicates_.Get(), 0);
            Move(cullPredicates_.Get(), predicateState_, D3D12_RESOURCE_STATE_PREDICATION);
        }
    }
    Stamp(23);
    Stamp(24);
    if (c.mode == CullMode::HiZ)
    {
        commands_->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        Move(gbuffer_[4].Get(), gStates_[4], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Move(cullPyramid_.Get(), pyramidState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commands_->SetPipelineState(buildHiZ_.Get());
        UINT offset = 0, w = paddedWidth_, h = paddedHeight_, slot = 0;
        constants.level = {0, 0, w, h};
        constants.info.y = 0;
        BindCulling(slot++, constants, true);
        commands_->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
        auto barrier = [&]()
        {
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            b.UAV.pResource = cullPyramid_.Get();
            commands_->ResourceBarrier(1, &b);
        };
        barrier();
        while (w > 1 || h > 1)
        {
            UINT next = offset + w * h;
            constants.level = {offset, next, w, h};
            constants.info.y = 1;
            BindCulling(slot++, constants, true);
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
            commands_->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
            barrier();
            offset = next;
        }
    }
    Stamp(25);
    Stamp(26);
    if (c.mode == CullMode::HiZ)
    {
        Move(cullPredicates_.Get(), predicateState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commands_->SetPipelineState(testHiZ_.Get());
        BindCulling(14, constants, true);
        commands_->Dispatch((c.total + 63) / 64, 1, 1);
        Move(cullPredicates_.Get(), predicateState_, D3D12_RESOURCE_STATE_PREDICATION);
        Move(gbuffer_[4].Get(), gStates_[4], D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }
    Stamp(27);
}
void Sample::CullingDebug()
{
    auto &c = panel_.culling;
    if (c.diagnostic != 2 && c.diagnostic != 3)
        return;
    auto target = sceneRtv_->GetCPUDescriptorHandleForHeapStart();
    target.ptr += 4 * rtvStride_;
    commands_->OMSetRenderTargets(1, &target, FALSE, nullptr);
    Move(cullPyramid_.Get(), pyramidState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    CullConstants constants{};
    constants.dimensions = {width_, height_, paddedWidth_, paddedHeight_};
    constants.info.z = UINT(c.mip);
    if (c.mode == CullMode::Software)
    {
        constants.info.w = 2;
        constants.level.z = c.software.width;
        constants.level.w = c.software.height;
    }
    else if (c.mode == CullMode::HiZ)
    {
        constants.info.w = 1;
        if (c.diagnostic == 2)
            constants.info.z = 0;
    }
    BindCulling(15, constants, false);
    commands_->SetPipelineState(cullDebugPipeline_.Get());
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands_->DrawInstanced(3, 1, 0, 0);
}
void Sample::CopyCullingResults()
{
    if (!panel_.culling.Conditional())
        return;
    Move(cullPredicates_.Get(), predicateState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands_->CopyResource(cullReadback_.Get(), cullPredicates_.Get());
}
void Sample::ReadCullingResults()
{
    auto &c = panel_.culling;
    if (!c.Conditional())
        return;
    void *mapped = nullptr;
    D3D12_RANGE range{0, 4600 * 8};
    CheckHR(cullReadback_->Map(0, &range, &mapped), "Culling diagnostic readback");
    const auto *data = static_cast<const uint64_t *>(mapped);
    std::vector<uint64_t> flags(c.total, 1);
    for (UINT i = 0; i < c.total; ++i)
        if (c.mode == CullMode::HiZ)
            flags[i] = data[i];
        else if (querySlots_[i] >= 0)
            flags[i] = data[querySlots_[i]];
    c.Results(flags.data());
    cullReadback_->Unmap(0, nullptr);
}

void Sample::DumpCullingData(const std::filesystem::path &directory)
{
    auto &c = panel_.culling;
    if (c.mode == CullMode::Software)
    {
        std::ofstream f(directory / "software-depth.f32", std::ios::binary);
        f.write(reinterpret_cast<const char *>(c.software.pixels.data()), c.software.pixels.size() * 4);
    }
    if (c.mode != CullMode::HiZ)
        return;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    auto desc = CullBuffer(uint64_t(pyramidCount_) * 4);
    ComPtr<ID3D12Resource> readback;
    CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&readback)),
            "Pyramid readback");
    CheckHR(allocator_->Reset(), "Pyramid allocator");
    CheckHR(commands_->Reset(allocator_.Get(), nullptr), "Pyramid commands");
    auto before = pyramidState_;
    Move(cullPyramid_.Get(), pyramidState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands_->CopyResource(readback.Get(), cullPyramid_.Get());
    Move(cullPyramid_.Get(), pyramidState_, before);
    CheckHR(commands_->Close(), "Pyramid close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitForGpu();
    void *mapped = nullptr;
    D3D12_RANGE range{0, size_t(pyramidCount_) * 4};
    CheckHR(readback->Map(0, &range, &mapped), "Pyramid map");
    std::ofstream data(directory / "pyramid.f32", std::ios::binary);
    data.write(static_cast<const char *>(mapped), pyramidCount_ * 4);
    data.close();
    readback->Unmap(0, nullptr);
    nlohmann::json info = {{"width", paddedWidth_},
                           {"height", paddedHeight_},
                           {"viewportWidth", width_},
                           {"viewportHeight", height_},
                           {"count", pyramidCount_}};
    std::ofstream(directory / "pyramid.json") << info.dump(2) << '\n';
}
