#include "Sample.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <imgui_impl_win32.h>
#include <stdexcept>
using namespace tutorial;
namespace
{
void CheckVk(VkResult result, const char *operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(result) +
                                 ')');
}
void FontBarrier(VkCommandBuffer commands, VkImage image, VkImageLayout before, VkImageLayout after,
                 VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                 VkAccessFlags2 dstAccess)
{
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = before;
    barrier.newLayout = after;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}
} // namespace

void Sample::CreateImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendRendererName = "Shaderjp_Vulkan_OfflineHLSL";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    // Fixed font atlas only: do not advertise RendererHasTextures or dynamic font support.
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 3;
    if (!ImGui_ImplWin32_Init(window_))
        throw std::runtime_error("ImGui Win32 initialization failed");
    win32Ui_ = true;
    CreateUiPipeline();
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    uiFont_ =
        CreateImage(width, height, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    uiStaging_ = CreateBuffer(VkDeviceSize(width) * height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, pixels);
    CheckVk(vkResetCommandBuffer(commands_, 0), "ImGui font command reset");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(commands_, &begin), "ImGui font command begin");
    FontBarrier(commands_, uiFont_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {uint32_t(width), uint32_t(height), 1};
    vkCmdCopyBufferToImage(commands_, uiStaging_.buffer, uiFont_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copy);
    FontBarrier(commands_, uiFont_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    CheckVk(vkEndCommandBuffer(commands_), "ImGui font command end");
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = commands_;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    CheckVk(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE), "ImGui font submit");
    CheckVk(vkQueueWaitIdle(queue_), "ImGui font upload wait");
    DestroyBuffer(uiStaging_);
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    CheckVk(vkCreateSampler(device_, &sampler, nullptr, &uiSampler_), "ImGui sampler");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_, &properties);
    const auto alignment = properties.limits.minUniformBufferOffsetAlignment;
    uiConstants_ =
        CreateBuffer((64 + alignment - 1) / alignment * alignment, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                                          {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                                          {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 1;
    pool.poolSizeCount = 3;
    pool.pPoolSizes = sizes;
    CheckVk(vkCreateDescriptorPool(device_, &pool, nullptr, &uiDescriptorPool_), "ImGui descriptor pool");
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = uiDescriptorPool_;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &uiDescriptorLayout_;
    CheckVk(vkAllocateDescriptorSets(device_, &allocate, &uiDescriptorSet_), "ImGui descriptor set");
    VkDescriptorImageInfo image{VK_NULL_HANDLE, uiFont_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo filter{uiSampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkDescriptorBufferInfo projection{uiConstants_.buffer, 0, 64};
    VkWriteDescriptorSet writes[3]{};
    for (auto &write : writes)
    {
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = uiDescriptorSet_;
        write.descriptorCount = 1;
    }
    writes[0].dstBinding = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[0].pImageInfo = &image;
    writes[1].dstBinding = 2;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[1].pImageInfo = &filter;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[2].pBufferInfo = &projection;
    vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
    io.Fonts->SetTexID(ImTextureID(1)); // This renderer owns exactly one texture, identified by 1.
    Trace("Dear ImGui " IMGUI_VERSION " initialized: Win32 + offline SPIR-V renderer");
}

// CreateUiPipeline is written out here to keep all API operations visible in this project.
void Sample::CreateUiPipeline()
{
    if (!uiLayout_)
    {
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptors.bindingCount = 3;
        descriptors.pBindings = bindings;
        CheckVk(vkCreateDescriptorSetLayout(device_, &descriptors, nullptr, &uiDescriptorLayout_),
                "ImGui descriptor layout");
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &uiDescriptorLayout_;
        CheckVk(vkCreatePipelineLayout(device_, &layout, nullptr, &uiLayout_), "ImGui pipeline layout");
    }
    const auto vertex = ReadBinary(AssetPath(L"Shaders/ImGuiVertex.spv"));
    const auto pixel = ReadBinary(AssetPath(L"Shaders/ImGuiPixel.spv"));
    auto shader = [&](const std::vector<uint8_t> &bytes, VkShaderModule &module) {
        if (bytes.size() % 4 || bytes.size() < 20)
            throw std::runtime_error("Invalid SPIR-V binary");
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = bytes.size();
        info.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
        CheckVk(vkCreateShaderModule(device_, &info, nullptr, &module), "vkCreateShaderModule");
    };
    shader(vertex, uiVertexShader_);
    shader(pixel, uiPixelShader_);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = uiVertexShader_;
    stages[0].pName = "VSMain";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = uiPixelShader_;
    stages[1].pName = "PSMain";
    VkVertexInputBindingDescription binding{0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)},
        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)}};
    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(attributes));
    vertexInput.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_FALSE;
    depth.depthWriteEnable = VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat_;
    rendering.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = uiLayout_;
    CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &uiPipeline_),
            "vkCreateGraphicsPipelines");
    vkDestroyShaderModule(device_, uiVertexShader_, nullptr);
    uiVertexShader_ = VK_NULL_HANDLE;
    vkDestroyShaderModule(device_, uiPixelShader_, nullptr);
    uiPixelShader_ = VK_NULL_HANDLE;
}

void Sample::RenderImGui()
{
    const auto *data = ImGui::GetDrawData();
    if (!data || data->TotalVtxCount == 0 || data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0)
        return;
    const size_t vertexBytes = size_t(data->TotalVtxCount) * sizeof(ImDrawVert);
    const size_t indexBytes = size_t(data->TotalIdxCount) * sizeof(ImDrawIdx);
    // Fence wait in OnRender precedes both uniform and UI-buffer writes, including growth/reallocation.
    if (vertexBytes > uiVertexCapacity_)
    {
        DestroyBuffer(uiVertices_);
        uiVertices_ = CreateBuffer(vertexBytes + 65536, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        uiVertexCapacity_ = vertexBytes + 65536;
    }
    if (indexBytes > uiIndexCapacity_)
    {
        DestroyBuffer(uiIndices_);
        uiIndices_ = CreateBuffer(indexBytes + 16384, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        uiIndexCapacity_ = indexBytes + 16384;
    }
    auto *vertices = static_cast<uint8_t *>(uiVertices_.mapped),
         *indices = static_cast<uint8_t *>(uiIndices_.mapped);
    for (const auto *list : data->CmdLists)
    {
        const size_t vb = size_t(list->VtxBuffer.Size) * sizeof(ImDrawVert),
                     ib = size_t(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        std::memcpy(vertices, list->VtxBuffer.Data, vb);
        std::memcpy(indices, list->IdxBuffer.Data, ib);
        vertices += vb;
        indices += ib;
    }
    Flush(uiVertices_);
    Flush(uiIndices_);
    const float l = data->DisplayPos.x, r = l + data->DisplaySize.x;
    const float t = data->DisplayPos.y, b = t + data->DisplaySize.y;
    const float projection[16] = {2 / (r - l), 0, 0, 0,    0, 2 / (t - b),       0,
                                  0,           0, 0, 0.5f, 0, (r + l) / (l - r), (t + b) / (b - t),
                                  0.5f,        1};
    std::memcpy(uiConstants_.mapped, projection, sizeof(projection));
    Flush(uiConstants_);
    auto setup = [&] {
        VkViewport viewport{0, float(height_), float(width_), -float(height_), 0, 1};
        vkCmdSetViewport(commands_, 0, 1, &viewport);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipeline_);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commands_, 0, 1, &uiVertices_.buffer, &offset);
        vkCmdBindIndexBuffer(commands_, uiIndices_.buffer, 0,
                             sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, uiLayout_, 0, 1,
                                &uiDescriptorSet_, 0, nullptr);
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
            if (draw.GetTexID() != ImTextureID(1))
                throw std::runtime_error("Sample 09 ImGui renderer supports only its static font atlas");
            const int32_t x = int32_t(std::clamp(std::floor((draw.ClipRect.x - l) * data->FramebufferScale.x),
                                                 0.0f, float(width_)));
            const int32_t y = int32_t(std::clamp(std::floor((draw.ClipRect.y - t) * data->FramebufferScale.y),
                                                 0.0f, float(height_)));
            const int32_t right = int32_t(
                std::clamp(std::ceil((draw.ClipRect.z - l) * data->FramebufferScale.x), 0.0f, float(width_)));
            const int32_t bottom = int32_t(std::clamp(
                std::ceil((draw.ClipRect.w - t) * data->FramebufferScale.y), 0.0f, float(height_)));
            if (right <= x || bottom <= y)
                continue;
            VkRect2D clip{{x, y}, {uint32_t(right - x), uint32_t(bottom - y)}};
            vkCmdSetScissor(commands_, 0, 1, &clip);
            vkCmdDrawIndexed(commands_, draw.ElemCount, 1, draw.IdxOffset + indexOffset,
                             int32_t(draw.VtxOffset + vertexOffset), 0);
        }
        vertexOffset += list->VtxBuffer.Size;
        indexOffset += list->IdxBuffer.Size;
    }
}

void Sample::DestroyImGui() noexcept
{
    uiVertexCapacity_ = uiIndexCapacity_ = 0;
    if (device_)
    {
        DestroyBuffer(uiVertices_);
        DestroyBuffer(uiIndices_);
        DestroyBuffer(uiStaging_);
        DestroyBuffer(uiConstants_);
        DestroyImage(uiFont_);
        if (uiSampler_)
            vkDestroySampler(device_, uiSampler_, nullptr);
        if (uiDescriptorPool_)
            vkDestroyDescriptorPool(device_, uiDescriptorPool_, nullptr);
        if (uiPipeline_)
            vkDestroyPipeline(device_, uiPipeline_, nullptr);
        if (uiLayout_)
            vkDestroyPipelineLayout(device_, uiLayout_, nullptr);
        if (uiDescriptorLayout_)
            vkDestroyDescriptorSetLayout(device_, uiDescriptorLayout_, nullptr);
        if (uiVertexShader_)
            vkDestroyShaderModule(device_, uiVertexShader_, nullptr);
        if (uiPixelShader_)
            vkDestroyShaderModule(device_, uiPixelShader_, nullptr);
        uiSampler_ = VK_NULL_HANDLE;
        uiDescriptorPool_ = VK_NULL_HANDLE;
        uiPipeline_ = VK_NULL_HANDLE;
        uiLayout_ = VK_NULL_HANDLE;
        uiDescriptorLayout_ = VK_NULL_HANDLE;
        uiVertexShader_ = uiPixelShader_ = VK_NULL_HANDLE;
    }
    if (ImGui::GetCurrentContext())
    {
        if (win32Ui_)
            ImGui_ImplWin32_Shutdown();
        win32Ui_ = false;
        ImGui::DestroyContext();
    }
}
