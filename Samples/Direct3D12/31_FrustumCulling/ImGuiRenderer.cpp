#include "Sample.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <imgui_impl_win32.h>
#include <stdexcept>
using namespace tutorial;

void Sample::CreateImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendRendererName = "Shaderjp_D3D12_OfflineHLSL";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    // Static font atlas: this tutorial intentionally does not advertise RendererHasTextures.
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 3;
    if (!ImGui_ImplWin32_Init(window_))
        throw std::runtime_error("ImGui Win32 initialization failed");
    win32Ui_ = true;
    CreateUiPipeline();
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = width;
    texture.Height = height;
    texture.DepthOrArraySize = texture.MipLevels = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    CheckHR(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&uiFont_)),
            "Create ImGui font atlas");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    device_->GetCopyableFootprints(&texture, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    auto upload = UploadBuffer(nullptr, bytes);
    void *mapped = nullptr;
    D3D12_RANGE read{0, 0};
    CheckHR(upload->Map(0, &read, &mapped), "ImGui font upload Map");
    for (int row = 0; row < height; ++row)
        std::memcpy(static_cast<uint8_t *>(mapped) + footprint.Offset + row * footprint.Footprint.RowPitch,
                    pixels + size_t(row) * width * 4, size_t(width) * 4);
    upload->Unmap(0, nullptr);
    CheckHR(allocator_->Reset(), "ImGui upload allocator reset");
    CheckHR(commands_->Reset(allocator_.Get(), nullptr), "ImGui upload commands reset");
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
    source.pResource = upload.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    destination.pResource = uiFont_.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    commands_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {uiFont_.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                          D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    commands_->ResourceBarrier(1, &barrier);
    CheckHR(commands_->Close(), "ImGui upload commands close");
    ID3D12CommandList *lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitForGpu(); // The local staging resource may be released only after the copy finishes.
    const auto stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu = textureHeap_->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += fontSlot_ * stride;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = texture.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(uiFont_.Get(), &srv, cpu);
    auto gpu = textureHeap_->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += fontSlot_ * stride;
    io.Fonts->SetTexID(static_cast<ImTextureID>(gpu.ptr));
    Trace("Dear ImGui " IMGUI_VERSION " initialized: Win32 + offline DXIL renderer");
}

void Sample::CreateUiPipeline()
{
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    // A separate 64-byte UI projection, allocated on a 256-byte CB boundary.
    uiConstants_ = UploadBuffer(nullptr, 256);
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable = {1, &range};
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root{};
    root.NumParameters = 2;
    root.pParameters = parameters;
    root.NumStaticSamplers = 1;
    root.pStaticSamplers = &sampler;
    root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> serialized, error;
    CheckHR(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error),
            "Serialize ImGui root signature");
    CheckHR(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                         IID_PPV_ARGS(&uiRoot_)),
            "Create ImGui root signature");
    const auto vertex = ReadBinary(AssetPath(L"Shaders/ImGuiVertex.cso"));
    const auto pixel = ReadBinary(AssetPath(L"Shaders/ImGuiPixel.cso"));
    const D3D12_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ImDrawVert, pos),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ImDrawVert, uv),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(ImDrawVert, col),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = uiRoot_.Get();
    pso.VS = {vertex.data(), vertex.size()};
    pso.PS = {pixel.data(), pixel.size()};
    pso.InputLayout = {elements, static_cast<UINT>(std::size(elements))};
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    auto &blend = pso.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    pso.SampleDesc.Count = 1;
    CheckHR(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&uiPipeline_)), "Create ImGui PSO");
}

void Sample::RenderImGui()
{
    const auto *data = ImGui::GetDrawData();
    if (!data || data->TotalVtxCount == 0 || data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0)
        return;
    // The scene fence has already completed. Reuse or grow this frame's CPU-visible UI buffers.
    const auto vertexBytes = size_t(data->TotalVtxCount) * sizeof(ImDrawVert);
    const auto indexBytes = size_t(data->TotalIdxCount) * sizeof(ImDrawIdx);
    if (vertexBytes > uiVertexCapacity_)
    {
        uiVertices_ = UploadBuffer(nullptr, vertexBytes + 65536);
        uiVertexCapacity_ = vertexBytes + 65536;
    }
    if (indexBytes > uiIndexCapacity_)
    {
        uiIndices_ = UploadBuffer(nullptr, indexBytes + 16384);
        uiIndexCapacity_ = indexBytes + 16384;
    }
    void *vertexMapped = nullptr, *indexMapped = nullptr;
    D3D12_RANGE read{0, 0};
    CheckHR(uiVertices_->Map(0, &read, &vertexMapped), "ImGui vertex Map");
    CheckHR(uiIndices_->Map(0, &read, &indexMapped), "ImGui index Map");
    auto *vertices = static_cast<uint8_t *>(vertexMapped), *indices = static_cast<uint8_t *>(indexMapped);
    for (const auto *list : data->CmdLists)
    {
        const size_t vb = size_t(list->VtxBuffer.Size) * sizeof(ImDrawVert),
                     ib = size_t(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        std::memcpy(vertices, list->VtxBuffer.Data, vb);
        std::memcpy(indices, list->IdxBuffer.Data, ib);
        vertices += vb;
        indices += ib;
    }
    uiVertices_->Unmap(0, nullptr);
    uiIndices_->Unmap(0, nullptr);
    D3D12_VERTEX_BUFFER_VIEW vertexView{uiVertices_->GetGPUVirtualAddress(), static_cast<UINT>(vertexBytes),
                                        sizeof(ImDrawVert)};
    D3D12_INDEX_BUFFER_VIEW indexView{uiIndices_->GetGPUVirtualAddress(), static_cast<UINT>(indexBytes),
                                      sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT};
    const float l = data->DisplayPos.x, r = l + data->DisplaySize.x;
    const float t = data->DisplayPos.y, b = t + data->DisplaySize.y;
    const float projection[16] = {2 / (r - l), 0, 0, 0,    0, 2 / (t - b),       0,
                                  0,           0, 0, 0.5f, 0, (r + l) / (l - r), (t + b) / (b - t),
                                  0.5f,        1};
    void *projectionMapped = nullptr;
    CheckHR(uiConstants_->Map(0, &read, &projectionMapped), "ImGui constant Map");
    std::memcpy(projectionMapped, projection, sizeof(projection));
    uiConstants_->Unmap(0, nullptr);
    auto font = textureHeap_->GetGPUDescriptorHandleForHeapStart();
    font.ptr += fontSlot_ * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto setup = [&]
    {
        D3D12_VIEWPORT viewport{0, 0, float(width_), float(height_), 0, 1};
        commands_->RSSetViewports(1, &viewport);
        commands_->SetPipelineState(uiPipeline_.Get());
        commands_->SetGraphicsRootSignature(uiRoot_.Get());
        ID3D12DescriptorHeap *heaps[] = {textureHeap_.Get()};
        commands_->SetDescriptorHeaps(1, heaps);
        commands_->SetGraphicsRootConstantBufferView(0, uiConstants_->GetGPUVirtualAddress());
        commands_->SetGraphicsRootDescriptorTable(1, font);
        commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands_->IASetVertexBuffers(0, 1, &vertexView);
        commands_->IASetIndexBuffer(&indexView);
        const float factor[4] = {};
        commands_->OMSetBlendFactor(factor);
    };
    setup();
    uint32_t vertexOffset = 0, indexOffset = 0;
    for (const auto *list : data->CmdLists)
    {
        for (const auto &draw : list->CmdBuffer)
        {
            if (draw.UserCallback)
            {
                if (draw.UserCallback == ImDrawCallback_ResetRenderState)
                    setup();
                else
                    draw.UserCallback(list, &draw);
                continue;
            }
            if (draw.GetTexID() != static_cast<ImTextureID>(font.ptr))
                throw std::runtime_error("Sample 09 ImGui renderer supports only its static font atlas");
            D3D12_RECT clip{LONG(std::clamp(std::floor((draw.ClipRect.x - l) * data->FramebufferScale.x),
                                            0.0f, float(width_))),
                            LONG(std::clamp(std::floor((draw.ClipRect.y - t) * data->FramebufferScale.y),
                                            0.0f, float(height_))),
                            LONG(std::clamp(std::ceil((draw.ClipRect.z - l) * data->FramebufferScale.x), 0.0f,
                                            float(width_))),
                            LONG(std::clamp(std::ceil((draw.ClipRect.w - t) * data->FramebufferScale.y), 0.0f,
                                            float(height_)))};
            if (clip.right <= clip.left || clip.bottom <= clip.top)
                continue;
            commands_->RSSetScissorRects(1, &clip);
            commands_->DrawIndexedInstanced(draw.ElemCount, 1, draw.IdxOffset + indexOffset,
                                            static_cast<INT>(draw.VtxOffset + vertexOffset), 0);
        }
        vertexOffset += list->VtxBuffer.Size;
        indexOffset += list->IdxBuffer.Size;
    }
}

void Sample::DestroyImGui() noexcept
{
    uiVertices_.Reset();
    uiIndices_.Reset();
    uiConstants_.Reset();
    uiPipeline_.Reset();
    uiRoot_.Reset();
    uiFont_.Reset();
    if (ImGui::GetCurrentContext())
    {
        if (win32Ui_)
            ImGui_ImplWin32_Shutdown();
        win32Ui_ = false;
        ImGui::DestroyContext();
    }
}
