#include "Sample.h"
#include <cstring>
using namespace tutorial;
namespace
{
void CullVk(VkResult result, const char *operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}
void CullBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 src, VkAccessFlags2 source,
                 VkPipelineStageFlags2 dst, VkAccessFlags2 destination)
{
    VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask = src;
    b.srcAccessMask = source;
    b.dstStageMask = dst;
    b.dstAccessMask = destination;
    VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    d.memoryBarrierCount = 1;
    d.pMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &d);
}
} // namespace
void Sample::CreateCullingSizeResources()
{
    if (!cullDescriptorLayout_)
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (uint32_t index : {0u, 1u, 3u, 4u, 5u, 6u})
            bindings.push_back({index,
                                index == 0   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                : index == 1 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                             : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = uint32_t(bindings.size());
        layout.pBindings = bindings.data();
        CullVk(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &cullDescriptorLayout_),
               "Culling descriptor layout");
        VkPipelineLayoutCreateInfo pipeline{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeline.setLayoutCount = 1;
        pipeline.pSetLayouts = &cullDescriptorLayout_;
        CullVk(vkCreatePipelineLayout(device_, &pipeline, nullptr, &cullLayout_), "Culling pipeline layout");
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1},
                                              {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                                              {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = 3;
        pool.pPoolSizes = sizes;
        CullVk(vkCreateDescriptorPool(device_, &pool, nullptr, &cullPool_), "Culling descriptor pool");
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = cullPool_;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &cullDescriptorLayout_;
        CullVk(vkAllocateDescriptorSets(device_, &allocate, &cullSet_), "Culling descriptor set");
        cullBounds_ = CreateBuffer(4600 * sizeof(CullBounds), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        cullConstants_ = CreateBuffer(16 * constantStride_, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (CULL_SAMPLE == 32 || CULL_SAMPLE == 34)
            usage |= VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;
        cullPredicates_ = CreateBuffer(4600 * 8, usage);
        VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        query.queryType = VK_QUERY_TYPE_OCCLUSION;
        query.queryCount = 4600;
        CullVk(vkCreateQueryPool(device_, &query, nullptr, &occlusionQueries_), "Occlusion query pool");
        beginConditional_ = reinterpret_cast<PFN_vkCmdBeginConditionalRenderingEXT>(
            vkGetDeviceProcAddr(device_, "vkCmdBeginConditionalRenderingEXT"));
        endConditional_ = reinterpret_cast<PFN_vkCmdEndConditionalRenderingEXT>(
            vkGetDeviceProcAddr(device_, "vkCmdEndConditionalRenderingEXT"));
        if ((CULL_SAMPLE == 32 || CULL_SAMPLE == 34) && (!beginConditional_ || !endConditional_))
            throw std::runtime_error("VK_EXT_conditional_rendering entry points required");
    }
    DestroyBuffer(cullPyramid_);
    DestroyBuffer(softwareUpload_);
    paddedWidth_ = CullPowerOfTwo(width_);
    paddedHeight_ = CullPowerOfTwo(height_);
    pyramidCount_ = 0;
    for (uint32_t w = paddedWidth_, h = paddedHeight_;; w = std::max(1u, w / 2), h = std::max(1u, h / 2))
    {
        pyramidCount_ += w * h;
        if (w == 1 && h == 1)
            break;
    }
    // The pyramid is GPU-only during rendering. HOST_VISIBLE memory can reside in system RAM
    // on a discrete GPU, making every reduction level pay PCIe traffic instead of testing Hi-Z.
    VkBufferCreateInfo pyramidInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    pyramidInfo.size = uint64_t(pyramidCount_) * 4;
    pyramidInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    CullVk(vkCreateBuffer(device_, &pyramidInfo, nullptr, &cullPyramid_.buffer), "Pyramid buffer");
    VkMemoryRequirements pyramidRequirements{};
    vkGetBufferMemoryRequirements(device_, cullPyramid_.buffer, &pyramidRequirements);
    VkMemoryAllocateInfo pyramidMemory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    pyramidMemory.allocationSize = pyramidRequirements.size;
    pyramidMemory.memoryTypeIndex =
        MemoryType(pyramidRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CullVk(vkAllocateMemory(device_, &pyramidMemory, nullptr, &cullPyramid_.memory), "Pyramid device memory");
    CullVk(vkBindBufferMemory(device_, cullPyramid_.buffer, cullPyramid_.memory, 0), "Pyramid memory bind");
    softwareUpload_ = CreateBuffer(uint64_t(width_) * height_ * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    VkDescriptorImageInfo image{VK_NULL_HANDLE, gbuffer_[4].view,
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo buffers[] = {
        {cullConstants_.buffer, 0, sizeof(CullConstants)},
        {cullBounds_.buffer, 0, VK_WHOLE_SIZE},
        {CULL_SAMPLE == 33 ? softwareUpload_.buffer : cullPyramid_.buffer, 0, VK_WHOLE_SIZE},
        {cullPyramid_.buffer, 0, VK_WHOLE_SIZE},
        {cullPredicates_.buffer, 0, VK_WHOLE_SIZE}};
    uint32_t b = 0;
    std::vector<VkWriteDescriptorSet> writes;
    for (uint32_t index : {0u, 1u, 3u, 4u, 5u, 6u})
    {
        VkWriteDescriptorSet d{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        d.dstSet = cullSet_;
        d.dstBinding = index;
        d.descriptorCount = 1;
        d.descriptorType = index == 0   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                           : index == 1 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                        : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        if (index == 1)
            d.pImageInfo = &image;
        else
            d.pBufferInfo = &buffers[b++];
        writes.push_back(d);
    }
    vkUpdateDescriptorSets(device_, uint32_t(writes.size()), writes.data(), 0, nullptr);
}
void Sample::BindCulling(uint32_t slot, const CullConstants &constants, bool compute)
{
    std::memcpy(static_cast<uint8_t *>(cullConstants_.mapped) + slot * constantStride_, &constants,
                sizeof(constants));
    Flush(cullConstants_);
    uint32_t offset = uint32_t(slot * constantStride_);
    vkCmdBindDescriptorSets(commands_,
                            compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                            cullLayout_, 0, 1, &cullSet_, 1, &offset);
}
void Sample::CullingPass()
{
    auto &c = panel_.culling;
    preserveCullDepth_ = false;
    querySlots_.assign(c.total, -1);
    queryCount_ = 0;
    CullConstants constants{};
    constants.viewProjection = c.viewProjection;
    constants.dimensions = {width_, height_, paddedWidth_, paddedHeight_};
    constants.info.x = c.total;
    std::memcpy(cullBounds_.mapped, c.bounds.data(), c.bounds.size() * sizeof(CullBounds));
    Flush(cullBounds_);
    if (c.mode == CullMode::Software && c.diagnostic == 2)
    {
        std::memcpy(softwareUpload_.mapped, c.software.pixels.data(), c.software.pixels.size() * 4);
        Flush(softwareUpload_);
    }
    Stamp(20);
    if (c.UsesDepth())
    {
        Begin({}, gbuffer_[4].view, true, false, width_, height_);
        DrawObjects(4);
        vkCmdEndRendering(commands_);
        preserveCullDepth_ = true;
        CullBarrier(commands_, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    }
    Stamp(21);
    Stamp(22);
    if (c.mode == CullMode::Query)
    {
        vkCmdResetQueryPool(commands_, occlusionQueries_, 0, 4600);
        Begin({}, gbuffer_[4].view, false, false, width_, height_);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, boundsPipeline_);
        for (uint32_t i = 0; i < c.total; ++i)
            if (c.Candidate(i) && !c.Occluder(i) && c.projected[i].safe)
            {
                uint32_t slot = queryCount_++;
                querySlots_[i] = int(slot);
                Bind(i + 4601, 0);
                vkCmdBeginQuery(commands_, occlusionQueries_, slot, 0);
                vkCmdDraw(commands_, 36, 1, 0, 0);
                vkCmdEndQuery(commands_, occlusionQueries_, slot);
            }
        vkCmdEndRendering(commands_);
        if (queryCount_)
        {
            vkCmdCopyQueryPoolResults(commands_, occlusionQueries_, 0, queryCount_, cullPredicates_.buffer, 0,
                                      8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            CullBarrier(commands_, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            constants.info.x = queryCount_;
            BindCulling(13, constants, true);
            vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, normalizeQueries_);
            vkCmdDispatch(commands_, (queryCount_ + 63) / 64, 1, 1);
            constants.info.x = c.total;
            CullBarrier(commands_, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT,
                        VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT);
        }
    }
    Stamp(23);
    Stamp(24);
    if (c.mode == CullMode::HiZ)
    {
        Move(gbuffer_[4].image, VK_IMAGE_ASPECT_DEPTH_BIT, gLayouts_[4],
             VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, buildHiZ_);
        uint32_t offset = 0, w = paddedWidth_, h = paddedHeight_, slot = 0;
        constants.level = {0, 0, w, h};
        constants.info.y = 0;
        BindCulling(slot++, constants, true);
        vkCmdDispatch(commands_, (w + 7) / 8, (h + 7) / 8, 1);
        auto barrier = [&]()
        {
            CullBarrier(commands_, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        };
        barrier();
        while (w > 1 || h > 1)
        {
            uint32_t next = offset + w * h;
            constants.level = {offset, next, w, h};
            constants.info.y = 1;
            BindCulling(slot++, constants, true);
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
            vkCmdDispatch(commands_, (w + 7) / 8, (h + 7) / 8, 1);
            barrier();
            offset = next;
        }
    }
    Stamp(25);
    Stamp(26);
    if (c.mode == CullMode::HiZ)
    {
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, testHiZ_);
        BindCulling(14, constants, true);
        vkCmdDispatch(commands_, (c.total + 63) / 64, 1, 1);
        CullBarrier(commands_, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT |
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        Move(gbuffer_[4].image, VK_IMAGE_ASPECT_DEPTH_BIT, gLayouts_[4],
             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }
    Stamp(27);
    if (c.Conditional())
        // Diagnostic CPU reads occur only after the submission fence. Also make device writes
        // available to the host memory domain before invalidating non-coherent allocations.
        CullBarrier(commands_, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
}
void Sample::CullingDebug()
{
    auto &c = panel_.culling;
    if (c.diagnostic != 2 && c.diagnostic != 3)
        return;
    CullConstants constants{};
    constants.dimensions = {width_, height_, paddedWidth_, paddedHeight_};
    constants.info.z = uint32_t(c.mip);
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
    Begin({hdr_[0].view}, VK_NULL_HANDLE, false, false, width_, height_);
    BindCulling(15, constants, false);
    vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, cullDebugPipeline_);
    vkCmdDraw(commands_, 3, 1, 0, 0);
    vkCmdEndRendering(commands_);
}
void Sample::ReadCullingResults()
{
    auto &c = panel_.culling;
    if (!c.Conditional())
        return;
    if (!cullPredicates_.coherent)
    {
        VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        r.memory = cullPredicates_.memory;
        r.size = VK_WHOLE_SIZE;
        CullVk(vkInvalidateMappedMemoryRanges(device_, 1, &r), "Culling result invalidate");
    }
    const auto *data = static_cast<const uint64_t *>(cullPredicates_.mapped);
    std::vector<uint64_t> flags(c.total, 1);
    for (uint32_t i = 0; i < c.total; ++i)
        if (c.mode == CullMode::HiZ)
            flags[i] = data[i];
        else if (querySlots_[i] >= 0)
            flags[i] = data[querySlots_[i]];
    c.Results(flags.data());
}
void Sample::DestroyCulling()
{
    if (!device_)
        return;
    for (auto *b : {&cullBounds_, &cullConstants_, &cullPredicates_, &cullPyramid_, &softwareUpload_})
        DestroyBuffer(*b);
    if (occlusionQueries_)
        vkDestroyQueryPool(device_, occlusionQueries_, nullptr);
    occlusionQueries_ = VK_NULL_HANDLE;
    if (cullPool_)
        vkDestroyDescriptorPool(device_, cullPool_, nullptr);
    cullPool_ = VK_NULL_HANDLE;
    if (cullLayout_)
        vkDestroyPipelineLayout(device_, cullLayout_, nullptr);
    cullLayout_ = VK_NULL_HANDLE;
    if (cullDescriptorLayout_)
        vkDestroyDescriptorSetLayout(device_, cullDescriptorLayout_, nullptr);
    cullDescriptorLayout_ = VK_NULL_HANDLE;
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
    auto readback = CreateBuffer(uint64_t(pyramidCount_) * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    CullVk(vkResetCommandBuffer(commands_, 0), "Pyramid readback reset");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CullVk(vkBeginCommandBuffer(commands_, &begin), "Pyramid readback begin");
    CullBarrier(commands_, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferCopy copy{0, 0, uint64_t(pyramidCount_) * 4};
    vkCmdCopyBuffer(commands_, cullPyramid_.buffer, readback.buffer, 1, &copy);
    CullBarrier(commands_, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    Submit();
    if (!readback.coherent)
    {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = readback.memory;
        range.size = VK_WHOLE_SIZE;
        CullVk(vkInvalidateMappedMemoryRanges(device_, 1, &range), "Pyramid invalidate");
    }
    std::ofstream data(directory / "pyramid.f32", std::ios::binary);
    data.write(static_cast<const char *>(readback.mapped), pyramidCount_ * 4);
    data.close();
    DestroyBuffer(readback);
    nlohmann::json info = {{"width", paddedWidth_},
                           {"height", paddedHeight_},
                           {"viewportWidth", width_},
                           {"viewportHeight", height_},
                           {"count", pyramidCount_}};
    std::ofstream(directory / "pyramid.json") << info.dump(2) << '\n';
}
