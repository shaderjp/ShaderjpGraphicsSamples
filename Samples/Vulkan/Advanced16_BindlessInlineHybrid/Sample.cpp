#include "Sample.h"
#include <cstring>
#include <imgui_impl_win32.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
using namespace tutorial;
using namespace DirectX;
namespace
{
void CheckVk(VkResult r, const char *op)
{
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(op) + " failed: " + std::to_string(r));
}
VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                            VkDebugUtilsMessageTypeFlagsEXT,
                                            const VkDebugUtilsMessengerCallbackDataEXT *data, void *)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        ValidationError(data->pMessage);
    else
        Trace(data->pMessage);
    return VK_FALSE;
}
void ImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout before,
                  VkImageLayout after, VkPipelineStageFlags2 src, VkAccessFlags2 sa,
                  VkPipelineStageFlags2 dst, VkAccessFlags2 da, uint32_t mips = 1, uint32_t layers = 1)
{
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = src;
    b.srcAccessMask = sa;
    b.dstStageMask = dst;
    b.dstAccessMask = da;
    b.oldLayout = before;
    b.newLayout = after;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {aspect, 0, mips, 0, layers};
    VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    d.imageMemoryBarrierCount = 1;
    d.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &d);
}
void HostReadBarrier(VkCommandBuffer commands, VkBuffer buffer)
{
    VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    b.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer = buffer;
    b.size = VK_WHOLE_SIZE;
    VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    d.bufferMemoryBarrierCount = 1;
    d.pBufferMemoryBarriers = &b;
    vkCmdPipelineBarrier2(commands, &d);
}
} // namespace
void Sample::OnInit(HWND window, uint32_t width, uint32_t height, const Options &options)
{
    if(options.preset==L"Full")throw std::runtime_error("Inline RT supports OPAQUE/MASK; Full BLEND is unsupported");
    options_ = options;
    window_ = window;
    width_ = width;
    height_ = height;
    timing_.api = "Vulkan";
    auto start = std::chrono::steady_clock::now();
    data_ = LoadGallery(options_);
    timing_.loadMs = GalleryMs(start);
    panel_.Initialize(data_, options_);
    initialOptions_ = options_;
    initialOptions_.galleryObjects = data_.objects;
    initialOptions_.galleryMaterials = data_.materialCount;
    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
                                VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    const char *layer = "VK_LAYER_KHRONOS_validation";
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Shader.jp BindlessGallery";
    app.apiVersion = VK_API_VERSION_1_3;
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = DebugMessage;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = extensions;
#ifdef _DEBUG
    info.enabledLayerCount = 1;
    info.ppEnabledLayerNames = &layer;
    info.enabledExtensionCount = 3;
    info.pNext = &debug;
#else
    (void)layer;
#endif
    CheckVk(vkCreateInstance(&info, nullptr, &instance_), "Instance");
#ifdef _DEBUG
    auto createDebug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (!createDebug)
        throw std::runtime_error("Debug utils missing");
    CheckVk(createDebug(instance_, &debug, nullptr, &debug_), "Debug messenger");
    Trace("Vulkan Validation Layer enabled");
#endif
    VkWin32SurfaceCreateInfoKHR surface{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    surface.hinstance = GetModuleHandleW(nullptr);
    surface.hwnd = window_;
    CheckVk(vkCreateWin32SurfaceKHR(instance_, &surface, nullptr, &surface_), "Win32 surface");
    uint32_t count = 0;
    CheckVk(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "Physical devices");
    std::vector<VkPhysicalDevice> devices(count);
    CheckVk(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "Physical devices");
    for (auto candidate : devices)
    {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(candidate, &p);
        if (p.apiVersion < VK_API_VERSION_1_3)
            continue;
        uint32_t n = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
        std::vector<VkQueueFamilyProperties> families(n);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, families.data());
        for (uint32_t q = 0; q < n; ++q)
        {
            VkBool32 present = FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, q, surface_, &present);
            if (present &&
                (families[q].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                    (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT) &&
                (!physical_ || p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))
            {
                physical_ = candidate;
                queueFamily_ = q;
                timestampBits_ = families[q].timestampValidBits;
            }
        }
    }
    if (!physical_ || !timestampBits_)
        throw std::runtime_error("Vulkan 1.3 graphics+compute+present queue with timestamps required");
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_, &props);
    timing_.gpuName = props.deviceName;
    timing_.driver = std::to_string(props.driverVersion);
    timestampPeriod_ = props.limits.timestampPeriod;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceMeshShaderFeaturesEXT mesh{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR rt{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    supported.pNext = &f13;
    f13.pNext = &f12;
    if constexpr (GALLERY_TECHNIQUE >= 14)
    {
        f12.pNext = &as;
        as.pNext = &rt;
    }
    if constexpr (GALLERY_TECHNIQUE == 13)
    {
        f12.pNext = &mesh;
    }
    if constexpr (GALLERY_TECHNIQUE == 15)
    {
        rt.pNext = &mesh;
    }
    vkGetPhysicalDeviceFeatures2(physical_, &supported);
    if (!f13.dynamicRendering || !f13.synchronization2 || !f13.shaderDemoteToHelperInvocation ||
        !f12.runtimeDescriptorArray || !f12.shaderSampledImageArrayNonUniformIndexing ||
        !f12.shaderStorageBufferArrayNonUniformIndexing || !f12.shaderStorageImageArrayNonUniformIndexing ||
        !supported.features.shaderStorageBufferArrayDynamicIndexing ||
        !supported.features.shaderStorageImageArrayDynamicIndexing)
        throw std::runtime_error(
            "Dynamic rendering, synchronization2, runtime descriptor arrays and non-uniform "
            "image/buffer indexing required");
    if (props.limits.maxPerStageDescriptorSampledImages < 404 ||
        props.limits.maxPerStageDescriptorStorageBuffers < 325 ||
        props.limits.maxPerStageDescriptorStorageImages < 5 ||
        props.limits.minStorageBufferOffsetAlignment > 256 || props.limits.maxImageArrayLayers < 256)
        throw std::runtime_error("Insufficient Gallery descriptor/array-layer limits");
    if constexpr (GALLERY_TECHNIQUE >= 14)
        if (!f12.bufferDeviceAddress || !as.accelerationStructure || !rt.rayQuery)
            throw std::runtime_error("KHR ray tracing and buffer device address required");
    if constexpr (GALLERY_TECHNIQUE == 13 || GALLERY_TECHNIQUE == 15)
        if (!mesh.meshShader)
            throw std::runtime_error("VK_EXT_mesh_shader required");
    // Enable only features used by this sample; query structures are reset first.
    f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = f13.synchronization2 = f13.shaderDemoteToHelperInvocation = VK_TRUE;
    f13.pNext = &f12;
    f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.runtimeDescriptorArray = f12.shaderSampledImageArrayNonUniformIndexing =
        f12.shaderStorageBufferArrayNonUniformIndexing = f12.shaderStorageImageArrayNonUniformIndexing =
            VK_TRUE;
    mesh = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    mesh.meshShader = VK_TRUE;
    as = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    as.accelerationStructure = VK_TRUE;
    rt = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    rt.rayQuery = VK_TRUE;
    std::vector<const char *> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if constexpr (GALLERY_TECHNIQUE >= 14)
    {
        f12.bufferDeviceAddress = VK_TRUE;
        f12.pNext = &as;
        as.pNext = &rt;
        deviceExtensions.insert(deviceExtensions.end(), {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                                                         VK_KHR_RAY_QUERY_EXTENSION_NAME,
                                                         VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME});
    }
    if constexpr (GALLERY_TECHNIQUE == 13 || GALLERY_TECHNIQUE == 15)
    {
        deviceExtensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
        if constexpr (GALLERY_TECHNIQUE == 13)
            f12.pNext = &mesh;
        else
            rt.pNext = &mesh;
    }
    float priority = 1;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = queueFamily_;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    VkPhysicalDeviceFeatures basic{};
    basic.shaderStorageBufferArrayDynamicIndexing = basic.shaderStorageImageArrayDynamicIndexing = VK_TRUE;
    VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device.pNext = &f13;
    device.pEnabledFeatures = &basic;
    device.queueCreateInfoCount = 1;
    device.pQueueCreateInfos = &q;
    device.enabledExtensionCount = uint32_t(deviceExtensions.size());
    device.ppEnabledExtensionNames = deviceExtensions.data();
    CheckVk(vkCreateDevice(physical_, &device, nullptr, &device_), "Device");
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
#define LOAD(field, name)                                                                                    \
    field = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device_, #name));                               \
    if (!field)                                                                                              \
    throw std::runtime_error("Missing " #name)
    if constexpr (GALLERY_TECHNIQUE >= 14)
    {
        LOAD(createAS_, vkCreateAccelerationStructureKHR);
        LOAD(destroyAS_, vkDestroyAccelerationStructureKHR);
        LOAD(getBuildSizes_, vkGetAccelerationStructureBuildSizesKHR);
        LOAD(buildAS_, vkCmdBuildAccelerationStructuresKHR);
        LOAD(getASAddress_, vkGetAccelerationStructureDeviceAddressKHR);
        VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        p.pNext = &asProperties_;
        vkGetPhysicalDeviceProperties2(physical_, &p);
    }
    if constexpr (GALLERY_TECHNIQUE == 13 || GALLERY_TECHNIQUE == 15)
    {
        LOAD(drawMesh_, vkCmdDrawMeshTasksEXT);
        LOAD(drawMeshIndirect_, vkCmdDrawMeshTasksIndirectEXT);
    }
#undef LOAD
    shaderStages_ = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    if constexpr (GALLERY_TECHNIQUE == 13 || GALLERY_TECHNIQUE == 15)
        shaderStages_ |= VK_SHADER_STAGE_MESH_BIT_EXT;
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queueFamily_;
    CheckVk(vkCreateCommandPool(device_, &pool, nullptr, &commandPool_), "Command pool");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = commandPool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    CheckVk(vkAllocateCommandBuffers(device_, &allocate, &commands_), "Commands");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    CheckVk(vkCreateFence(device_, &fence, nullptr, &fence_), "Fence");
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    CheckVk(vkCreateSemaphore(device_, &semaphore, nullptr, &imageAvailable_), "Acquire semaphore");
    VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    query.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query.queryCount = 12;
    CheckVk(vkCreateQueryPool(device_, &query, nullptr, &timestamps_), "Timestamp queries");
    CreateSwapchain();
    CreateScene();
    CreateSizeResources();
    CreatePipeline();
    CreateImGui();
    Trace("GPU: " + timing_.gpuName);
}
void Sample::BeginCommands()
{
    CheckVk(vkResetCommandBuffer(commands_, 0), "Command reset");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(commands_, &begin), "Command begin");
}
void Sample::Move(VkImage image, VkImageAspectFlags aspect, VkImageLayout &layout, VkImageLayout next)
{
    ImageBarrier(
        commands_, image, aspect, layout, next,
        layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    layout = next;
}
void Sample::Barrier()
{
    VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask = b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    d.memoryBarrierCount = 1;
    d.pMemoryBarriers = &b;
    vkCmdPipelineBarrier2(commands_, &d);
}

Sample::ImageResource Sample::LoadTexture(const std::vector<uint32_t> &layers, uint32_t channel)
{
    std::vector<std::vector<Image>> pixels;
    VkDeviceSize bytes = 0;
    for (auto index : layers)
    {
        const auto &image = data_.images.at(index);
        pixels.push_back(GenerateMipChain(LoadGalleryImage(image), image.semantic, image.alphaCutoff));
        for (auto &m : pixels.back())
            bytes += m.rgba.size();
    }
    auto format = channel == 0 || channel == 3 ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    auto result = CreateImage(pixels[0][0].width, pixels[0][0].height, format,
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(pixels[0].size()), uint32_t(layers.size()),
                              panel_.binding == 0 && GALLERY_TECHNIQUE >= 14);
    auto staging = CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    std::vector<VkBufferImageCopy> copies;
    VkDeviceSize offset = 0;
    for (uint32_t layer = 0; layer < pixels.size(); ++layer)
        for (uint32_t mip = 0; mip < pixels[layer].size(); ++mip)
        {
            const auto &p = pixels[layer][mip];
            std::memcpy(static_cast<uint8_t *>(staging.mapped) + offset, p.rgba.data(), p.rgba.size());
            VkBufferImageCopy copy{};
            copy.bufferOffset = offset;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1};
            copy.imageExtent = {p.width, p.height, 1};
            copies.push_back(copy);
            offset += p.rgba.size();
        }
    Flush(staging);
    BeginCommands();
    ImageBarrier(commands_, result.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, uint32_t(pixels[0].size()),
                 uint32_t(layers.size()));
    vkCmdCopyBufferToImage(commands_, staging.buffer, result.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           uint32_t(copies.size()), copies.data());
    ImageBarrier(commands_, result.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, uint32_t(pixels[0].size()), uint32_t(layers.size()));
    Submit();
    DestroyBuffer(staging);
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device_, result.image, &requirements);
    timing_.committedImages += requirements.size;
    timing_.imageBytes += bytes;
    return result;
}
void Sample::CreateScene()
{
    auto start = std::chrono::steady_clock::now();
    timing_.imageBytes = timing_.bufferBytes = timing_.asBytes = 0;
    timing_.committedImages = timing_.committedBuffers = 0;
    auto upload = [&](std::span<const uint8_t> bytes) {
        auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if constexpr (GALLERY_TECHNIQUE >= 14)
            usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        timing_.bufferBytes += bytes.size();
        auto result =
            CreateBuffer(std::max(VkDeviceSize(4), VkDeviceSize(bytes.size())), usage, bytes.data());
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device_, result.buffer, &requirements);
        timing_.committedBuffers += requirements.size;
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
            textures_.push_back(LoadTexture({i}, data_.images[i].semantic == ImageSemantic::Color ? 0 : 1));
    constants_ = CreateBuffer(512, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    visibility_ = DeviceBuffer(1024 + data_.candidates.size() * 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                                       VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    CheckVk(vkCreateSampler(device_, &sampler, nullptr, &sampler_), "Gallery sampler");
    timing_.imageResources = textures_.size();
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
    for (uint32_t i = 0; i < 5; ++i)
    {
        DestroyImage(hdr_[i]);
        hdr_[i] = CreateImage(width_, height_, VK_FORMAT_R32G32B32A32_SFLOAT,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT);
        hdrLayouts_[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    DestroyImage(depth_);
    depth_ = CreateImage(width_, height_, VK_FORMAT_D32_SFLOAT,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT);
    depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    if (descriptorPool_)
        UpdateDescriptors();
    for (const auto &image : hdr_)
    {
        VkMemoryRequirements r;
        vkGetImageMemoryRequirements(device_, image.image, &r);
        timing_.attachmentCommitted += r.size;
    }
    VkMemoryRequirements r;
    vkGetImageMemoryRequirements(device_, depth_.image, &r);
    timing_.attachmentCommitted += r.size;
}
void Sample::UpdateDescriptors()
{
    for (uint32_t set = 0; set < descriptorSets_.size(); ++set)
    {
        bool display = set + 1 == descriptorSets_.size(), global = set + 2 == descriptorSets_.size();
        uint32_t imageCount = panel_.binding == 0 ? 4 : uint32_t(textures_.size());
        std::vector<VkDescriptorImageInfo> images(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            uint32_t index = i;
            if (panel_.binding == 0 && GALLERY_TECHNIQUE < 14)
                index = (&data_.materials[(display || global) ? 0 : boundMaterial_[set]].images.x)[i];
            images[i] = {VK_NULL_HANDLE, textures_[index].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }
        VkDescriptorBufferInfo constant{constants_.buffer, 0, sizeof(GalleryConstants)},
            visible{visibility_.buffer, 0, VK_WHOLE_SIZE};
        std::vector<VkDescriptorBufferInfo> buffers;
        for (const auto &b : buffers_)
            buffers.push_back({b.buffer, 0, VK_WHOLE_SIZE});
        if (panel_.binding < 2 && GALLERY_TECHNIQUE == 13 && !display && !global)
        {
            const auto &g = data_.geometries[boundGeometry_[set]];
            uint32_t refs = 0;
            for (uint32_t m = 0; m < g.offsets.w; ++m)
                refs += data_.meshlets[g.offsets.z + m].offsets.z;
            buffers[3].offset = VkDeviceSize(g.offsets.x) * 48;
            buffers[3].range = VkDeviceSize(g.lengths.x) * 48;
            buffers[4].offset = VkDeviceSize(g.offsets.y) * 4;
            buffers[4].range = VkDeviceSize(g.lengths.y) * 4;
            buffers[5].offset = VkDeviceSize(g.offsets.z) * 64;
            buffers[5].range = VkDeviceSize(g.offsets.w) * 64;
            buffers[6].offset = VkDeviceSize(g.lengths.z) * 4;
            buffers[6].range = VkDeviceSize(refs) * 4;
            buffers[7].offset = VkDeviceSize(g.lengths.w) * 4;
            buffers[7].range = VkDeviceSize(g.lengths.y) * 4;
        }
        VkDescriptorImageInfo sampler{sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        std::array<VkDescriptorImageInfo, 5> output{};
        std::array<VkDescriptorImageInfo, 4> input{};
        for (uint32_t i = 0; i < 5; ++i)
            output[i] = {VK_NULL_HANDLE, hdr_[i].view, VK_IMAGE_LAYOUT_GENERAL};
        for (uint32_t i = 0; i < 4; ++i)
            input[i] = {VK_NULL_HANDLE, hdr_[display ? 0 : i + 1].view, VK_IMAGE_LAYOUT_GENERAL};
        std::vector<VkWriteDescriptorSet> writes;
        auto write = [&](uint32_t binding, VkDescriptorType type, uint32_t count,
                         const VkDescriptorImageInfo *image, const VkDescriptorBufferInfo *buffer) {
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = descriptorSets_[set];
            w.dstBinding = binding;
            w.descriptorType = type;
            w.descriptorCount = count;
            w.pImageInfo = image;
            w.pBufferInfo = buffer;
            writes.push_back(w);
        };
        write(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, nullptr, &constant);
        write(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, imageCount, images.data(), nullptr);
        write(2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, &sampler, nullptr);
        write(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, uint32_t(buffers.size()), nullptr, buffers.data());
        write(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 5, output.data(), nullptr);
        write(5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4, input.data(), nullptr);
        write(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, nullptr, &visible);
        VkWriteDescriptorSetAccelerationStructureKHR as{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
        as.accelerationStructureCount = 1;
        as.pAccelerationStructures = &tlas_.object;
        if constexpr (GALLERY_TECHNIQUE >= 14)
        {
            write(6, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, nullptr, nullptr);
            writes.back().pNext = &as;
        }
        vkUpdateDescriptorSets(device_, uint32_t(writes.size()), writes.data(), 0, nullptr);
    }
}
void Sample::CreatePipeline()
{
    boundSetForInstance_.clear();
    boundGeometry_.clear();
    boundMaterial_.clear();
    if ((panel_.binding < 2 && GALLERY_TECHNIQUE == 13) || (panel_.binding == 0 && GALLERY_TECHNIQUE == 12))
    {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> pairs;
        for (auto instance : data_.instances)
        {
            auto key = std::pair(GALLERY_TECHNIQUE == 13 ? instance.ids.x : 0u,
                                 panel_.binding == 0 ? instance.ids.y : 0u);
            auto [it, added] = pairs.emplace(key, uint32_t(pairs.size()));
            if (added)
            {
                boundGeometry_.push_back(key.first);
                boundMaterial_.push_back(key.second);
            }
            boundSetForInstance_.push_back(it->second);
        }
    }
    else
    {
        boundGeometry_.push_back(0);
        boundMaterial_.push_back(0);
    }
    uint32_t imageCount = panel_.binding == 0 ? 4 : uint32_t(textures_.size()),
             sets = uint32_t(boundGeometry_.size() + 2);
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, shaderStages_, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, imageCount, shaderStages_, nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, shaderStages_, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, uint32_t(buffers_.size()), shaderStages_, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 5, shaderStages_, nullptr},
        {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4, shaderStages_, nullptr},
        {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, shaderStages_, nullptr}};
    if constexpr (GALLERY_TECHNIQUE >= 14)
        bindings.push_back({6, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, shaderStages_, nullptr});
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = uint32_t(bindings.size());
    layout.pBindings = bindings.data();
    CheckVk(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &descriptorLayout_),
            "Gallery descriptor layout");
    std::vector<VkDescriptorPoolSize> sizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sets},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, (imageCount + 4) * sets},
        {VK_DESCRIPTOR_TYPE_SAMPLER, sets},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, uint32_t(buffers_.size() + 1) * sets},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 5 * sets}};
    if constexpr (GALLERY_TECHNIQUE >= 14)
        sizes.push_back({VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, sets});
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = sets;
    pool.poolSizeCount = uint32_t(sizes.size());
    pool.pPoolSizes = sizes.data();
    CheckVk(vkCreateDescriptorPool(device_, &pool, nullptr, &descriptorPool_), "Gallery descriptor pool");
    std::vector<VkDescriptorSetLayout> layouts(sets, descriptorLayout_);
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = descriptorPool_;
    allocate.descriptorSetCount = sets;
    allocate.pSetLayouts = layouts.data();
    descriptorSets_.resize(sets);
    CheckVk(vkAllocateDescriptorSets(device_, &allocate, descriptorSets_.data()), "Gallery descriptor sets");
    UpdateDescriptors();
    // Vulkan does not expose the opaque descriptor pool's allocated byte size.
    timing_.descriptorBytes = 0;
    timing_.descriptorCount = 0;
    for (auto size : sizes)
        timing_.descriptorCount += size.descriptorCount;
    VkPushConstantRange push{shaderStages_, 0, 16};
    VkPipelineLayoutCreateInfo pipeline{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline.setLayoutCount = 1;
    pipeline.pSetLayouts = &descriptorLayout_;
    pipeline.pushConstantRangeCount = 1;
    pipeline.pPushConstantRanges = &push;
    CheckVk(vkCreatePipelineLayout(device_, &pipeline, nullptr, &pipelineLayout_), "Gallery pipeline layout");
    auto module = [&](const wchar_t *name) {
        auto bytes = ReadBinary(AssetPath(std::filesystem::path(L"Shaders") /
                                          (std::wstring(name) + std::to_wstring(panel_.binding) + L".spv")));
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = bytes.size();
        info.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
        VkShaderModule m;
        CheckVk(vkCreateShaderModule(device_, &info, nullptr, &m), "Gallery shader module");
        modules_.push_back(m);
        return m;
    };
    auto graphics = [&](bool mesh, bool display, VkPipeline &out) {
        VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                     {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        stages[0].stage = mesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = module(mesh ? L"Mesh" : display ? L"DisplayVertex" : L"RasterVertex");
        stages[0].pName = mesh ? "MSMain" : "VSMain";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = module(display ? L"DisplayPixel" : L"RasterPixel");
        stages[1].pName = "PSMain";
        VkPipelineVertexInputStateCreateInfo vertex{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo sample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        sample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = depth.depthWriteEnable = display ? VK_FALSE : VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        std::array<VkPipelineColorBlendAttachmentState, 4> attachments{};
        for (auto &a : attachments)
            a.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = display ? 1 : 4;
        blend.pAttachments = attachments.data();
        VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = states;
        VkFormat formats[4] = {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT,
                               VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};
        if (display)
            formats[0] = colorFormat_;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = display ? 1 : 4;
        rendering.pColorAttachmentFormats = formats;
        rendering.depthAttachmentFormat = display ? VK_FORMAT_UNDEFINED : VK_FORMAT_D32_SFLOAT;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = mesh ? nullptr : &vertex;
        info.pInputAssemblyState = mesh ? nullptr : &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &sample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout_;
        CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &out),
                "Gallery graphics pipeline");
    };
    graphics(false, false, raster_);
    graphics(false, true, display_);
    if constexpr (GALLERY_TECHNIQUE == 13 || GALLERY_TECHNIQUE == 15)
        graphics(true, false, mesh_);
    auto compute = [&](VkPipeline &out, const wchar_t *name, const char *entry) {
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = module(name);
        info.stage.pName = entry;
        info.layout = pipelineLayout_;
        CheckVk(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &out),
                "Gallery compute pipeline");
    };
    compute(light_, L"Light", "Light");
    compute(countReset_, L"CountReset", "CountReset");
    compute(countVisible_, L"CountVisible", "CountVisible");
    if constexpr (GALLERY_TECHNIQUE == 13 || GALLERY_TECHNIQUE == 15)
    {
        compute(reset_, L"Reset", "Reset");
        compute(cull_, L"Cull", "Cull");
        compute(finalize_, L"Finalize", "Finalize");
    }
    if constexpr (GALLERY_TECHNIQUE >= 14)
        CreateInlinePipeline();
}
void Sample::Bind(VkPipelineBindPoint point, uint32_t instance, bool display)
{
    bool local =
        point == VK_PIPELINE_BIND_POINT_GRAPHICS && !panel_.driven &&
        ((panel_.binding < 2 && GALLERY_TECHNIQUE == 13) || (panel_.binding == 0 && GALLERY_TECHNIQUE == 12));
    uint32_t index = display ? uint32_t(descriptorSets_.size() - 1)
                     : local ? boundSetForInstance_[instance]
                             : uint32_t(descriptorSets_.size() - 2);
    vkCmdBindDescriptorSets(commands_, point, pipelineLayout_, 0, 1, &descriptorSets_[index], 0, nullptr);
    uint32_t push[] = {instance, 0, uint32_t(panel_.driven), uint32_t(data_.candidates.size())};
    vkCmdPushConstants(commands_, pipelineLayout_, shaderStages_, 0, 16, push);
    ++timing_.binds;
}
void Sample::Stamp(uint32_t index)
{
    vkCmdWriteTimestamp2(commands_, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timestamps_, index);
}

VkDeviceAddress Sample::Address(const Buffer &buffer)
{
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    info.buffer = buffer.buffer;
    return vkGetBufferDeviceAddress(device_, &info);
}
void Sample::BuildAcceleration()
{
    auto build = [&](Acceleration &output, VkAccelerationStructureTypeKHR type,
                     const VkAccelerationStructureGeometryKHR &geometry, uint32_t primitives) {
        VkAccelerationStructureBuildGeometryInfoKHR info{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        info.type = type;
        info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        info.geometryCount = 1;
        info.pGeometries = &geometry;
        VkAccelerationStructureBuildSizesInfoKHR sizes{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        getBuildSizes_(device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &primitives, &sizes);
        output.storage = DeviceBuffer(sizes.accelerationStructureSize,
                                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);
        VkAccelerationStructureCreateInfoKHR create{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        create.buffer = output.storage.buffer;
        create.size = sizes.accelerationStructureSize;
        create.type = type;
        CheckVk(createAS_(device_, &create, nullptr, &output.object), "Acceleration structure");
        VkDeviceSize alignment = asProperties_.minAccelerationStructureScratchOffsetAlignment;
        auto scratch = DeviceBuffer(sizes.buildScratchSize + alignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        auto address = Address(scratch);
        info.scratchData.deviceAddress = (address + alignment - 1) / alignment * alignment;
        info.dstAccelerationStructure = output.object;
        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = primitives;
        const auto *ranges = &range;
        BeginCommands();
        buildAS_(commands_, 1, &info, &ranges);
        Barrier();
        Submit();
        DestroyBuffer(scratch);
        timing_.asBytes += sizes.accelerationStructureSize;
    };
    blas_.resize(data_.geometries.size());
    for (uint32_t i = 0; i < data_.geometries.size(); ++i)
    {
        const auto &g = data_.geometries[i];
        VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        auto &t = geometry.geometry.triangles;
        t = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
        t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        t.vertexData.deviceAddress = Address(buffers_[panel_.binding == 2 ? g.resources.x : 3]) +
                                     (panel_.binding == 2 ? 0 : VkDeviceSize(g.offsets.x) * 48);
        t.vertexStride = 48;
        t.maxVertex = g.lengths.x - 1;
        t.indexType = VK_INDEX_TYPE_UINT32;
        t.indexData.deviceAddress = Address(buffers_[panel_.binding == 2 ? g.resources.y : 4]) +
                                    (panel_.binding == 2 ? 0 : VkDeviceSize(g.offsets.y) * 4);
        build(blas_[i], VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometry, g.lengths.y / 3);
    }
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    for (uint32_t i = 0; i < data_.instances.size(); ++i)
    {
        const auto &inst = data_.instances[i];
        VkAccelerationStructureInstanceKHR gpu{};
        XMFLOAT4X4 transpose;
        XMStoreFloat4x4(&transpose, XMMatrixTranspose(XMLoadFloat4x4(&inst.world)));
        std::memcpy(&gpu.transform, &transpose, 48);
        gpu.instanceCustomIndex = i;
        gpu.mask = 255;
        gpu.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        VkAccelerationStructureDeviceAddressInfoKHR info{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        info.accelerationStructure = blas_[inst.ids.x].object;
        gpu.accelerationStructureReference = getASAddress_(device_, &info);
        instances.push_back(gpu);
    }
    auto buffer = CreateBuffer(instances.size() * sizeof(instances[0]),
                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                               instances.data());
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
    geometry.geometry.instances.data.deviceAddress = Address(buffer);
    build(tlas_, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, geometry, uint32_t(instances.size()));
    DestroyBuffer(buffer);
}
void Sample::OnUpdate(float seconds)
{
    auto start = std::chrono::steady_clock::now();
    if (!options_.benchmark)
    {
        ImGui_ImplWin32_NewFrame();
        panel_.QueueTestInput();
        ImGui::NewFrame();
        panel_.Draw(data_, "Vulkan", timing_.cpu[1], timing_.gpu[5]);
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
        CheckVk(vkDeviceWaitIdle(device_), "Rebuild completion");
        DestroyImGui();
        DestroyPipelines();
        DestroyScene();
        options_.binding = std::array<std::wstring, 3>{L"bound", L"textures", L"resources"}[panel_.binding];
        options_.galleryObjects = panel_.objects;
        options_.galleryMaterials = panel_.materials;
        if (panel_.objects != data_.objects || panel_.materials != data_.materialCount)
            data_ = LoadGallery(options_);
        CreateScene();
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
    std::memcpy(constants_.mapped, &frame_, sizeof(frame_));
    Flush(constants_);
    timing_.cpu[0] = GalleryMs(start);
}
void Sample::OnRender()
{
    auto start = std::chrono::steady_clock::now();
    timing_.draws = timing_.dispatches = timing_.binds = 0;
    CheckVk(vkWaitForFences(device_, 1, &fence_, VK_TRUE, 30000000000ull), "Frame completion");
    uint32_t index = 0;
    auto acquire =
        vkAcquireNextImageKHR(device_, swapchain_, 30000000000ull, imageAvailable_, VK_NULL_HANDLE, &index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
    {
        OnResize(width_, height_);
        return;
    }
    if (acquire != VK_SUBOPTIMAL_KHR)
        CheckVk(acquire, "Acquire image");
    if (!capturePath_.empty())
    {
        if (!canCapture_)
            throw std::runtime_error("Screenshot transfer unsupported");
        capture_ = CreateBuffer(VkDeviceSize(width_) * height_ * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }
    double acquireMs = GalleryMs(start);
    start = std::chrono::steady_clock::now();
    CheckVk(vkResetFences(device_, 1, &fence_), "Fence reset");
    BeginCommands();
    vkCmdResetQueryPool(commands_, timestamps_, 0, 12);
    Stamp(10);
    Stamp(0);
    if (panel_.driven)
    {
        Bind(VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, reset_);
        vkCmdDispatch(commands_, 1, 1, 1);
        Barrier();
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, cull_);
        vkCmdDispatch(commands_, (uint32_t(data_.candidates.size()) + 63) / 64, 1, 1);
        Barrier();
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, finalize_);
        vkCmdDispatch(commands_, 1, 1, 1);
        Barrier();
        timing_.dispatches += 3;
    }
    Stamp(1);
    Stamp(2);
    if constexpr (GALLERY_TECHNIQUE != 14)
    {
        for (uint32_t i = 1; i < 5; ++i)
            Move(hdr_[i].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[i],
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        Move(depth_.image, VK_IMAGE_ASPECT_DEPTH_BIT, depthLayout_,
             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        Begin({hdr_[1].view, hdr_[2].view, hdr_[3].view, hdr_[4].view}, depth_.view, true, false, width_,
              height_);
        Bind(VK_PIPELINE_BIND_POINT_GRAPHICS);
        if (panel_.driven)
        {
            vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_);
            drawMeshIndirect_(commands_, visibility_.buffer, 16, 1, 12);
            ++timing_.dispatches;
        }
        else
        {
            uint32_t last = ((panel_.binding < 2 && GALLERY_TECHNIQUE == 13) ||
                             (panel_.binding == 0 && GALLERY_TECHNIQUE == 12))
                                ? boundSetForInstance_[0]
                                : UINT32_MAX;
            for (uint32_t i = 0; i < data_.instances.size(); ++i)
            {
                auto g = data_.geometries[data_.instances[i].ids.x];
                uint32_t push[] = {i, 0, 0, uint32_t(data_.candidates.size())};
                vkCmdPushConstants(commands_, pipelineLayout_, shaderStages_, 0, 16, push);
                if (((panel_.binding < 2 && GALLERY_TECHNIQUE == 13) ||
                     (panel_.binding == 0 && GALLERY_TECHNIQUE == 12)) &&
                    last != boundSetForInstance_[i])
                {
                    last = boundSetForInstance_[i];
                    vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                                            &descriptorSets_[last], 0, nullptr);
                    ++timing_.binds;
                }
                if constexpr (GALLERY_TECHNIQUE == 12)
                {
                    vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, raster_);
                    vkCmdBindIndexBuffer(commands_, buffers_[4].buffer, VkDeviceSize(g.offsets.y) * 4,
                                         VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(commands_, g.lengths.y, 1, 0, 0, 0);
                    ++timing_.draws;
                }
                else
                {
                    vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_);
                    drawMesh_(commands_, g.offsets.w, 1, 1);
                    ++timing_.dispatches;
                }
            }
        }
        vkCmdEndRendering(commands_);
        for (uint32_t i = 1; i < 5; ++i)
            Move(hdr_[i].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[i], VK_IMAGE_LAYOUT_GENERAL);
    }
    else
        for (uint32_t i = 1; i < 5; ++i)
            Move(hdr_[i].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[i], VK_IMAGE_LAYOUT_GENERAL);
    Stamp(3);
    Stamp(4);
    Move(hdr_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[0], VK_IMAGE_LAYOUT_GENERAL);
    if constexpr (GALLERY_TECHNIQUE >= 14)
    {
        Bind(VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, inlinePipeline_);
        vkCmdDispatch(commands_, (width_ + 7) / 8, (height_ + 7) / 8, 1);
        ++timing_.dispatches;
    }
    Stamp(5);
    Stamp(6);
    if constexpr (GALLERY_TECHNIQUE < 14)
    {
        Bind(VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, light_);
        vkCmdDispatch(commands_, (width_ + 7) / 8, (height_ + 7) / 8, 1);
        ++timing_.dispatches;
    }
    Stamp(7);
    Barrier();
    Stamp(8);
    ImageBarrier(commands_, images_[index], VK_IMAGE_ASPECT_COLOR_BIT,
                 presented_[index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    Begin({views_[index]}, VK_NULL_HANDLE, true, false, width_, height_);
    Bind(VK_PIPELINE_BIND_POINT_GRAPHICS, 0, true);
    vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, display_);
    vkCmdDraw(commands_, 3, 1, 0, 0);
    ++timing_.draws;
    Stamp(9);
    Stamp(11);
    if (!options_.benchmark && panel_.visible)
        RenderImGui();
    vkCmdEndRendering(commands_);
    if (capture_.buffer)
    {
        ImageBarrier(commands_, images_[index], VK_IMAGE_ASPECT_COLOR_BIT,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {width_, height_, 1};
        vkCmdCopyImageToBuffer(commands_, images_[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               capture_.buffer, 1, &copy);
        HostReadBarrier(commands_, capture_.buffer);
    }
    ImageBarrier(
        commands_, images_[index], VK_IMAGE_ASPECT_COLOR_BIT,
        capture_.buffer ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        capture_.buffer ? VK_PIPELINE_STAGE_2_COPY_BIT : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        capture_.buffer ? VK_ACCESS_2_TRANSFER_READ_BIT : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE, 0);
    CheckVk(vkEndCommandBuffer(commands_), "Frame close");
    timing_.cpu[1] = GalleryMs(start);
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = imageAvailable_;
    wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = renderFinished_[index];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = commands_;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    start = std::chrono::steady_clock::now();
    CheckVk(vkQueueSubmit2(queue_, 1, &submit, fence_), "Frame submit");
    timing_.cpu[2] = GalleryMs(start);
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_[index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &index;
    start = std::chrono::steady_clock::now();
    auto result = vkQueuePresentKHR(queue_, &present);
    timing_.cpu[4] = GalleryMs(start);
    if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
        CheckVk(result, "Present");
    start = std::chrono::steady_clock::now();
    CheckVk(vkQueueWaitIdle(queue_), "GPU completion");
    timing_.cpu[3] = acquireMs + GalleryMs(start);
    presented_[index] = true;
    std::array<uint64_t, 12> ticks{};
    CheckVk(vkGetQueryPoolResults(device_, timestamps_, 0, 12, sizeof(ticks), ticks.data(), 8,
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
            "Timestamp results");
    uint64_t mask = timestampBits_ == 64 ? UINT64_MAX : (uint64_t(1) << timestampBits_) - 1;
    for (size_t i = 0; i < 6; ++i)
        timing_.gpu[i] = double((ticks[i * 2 + 1] - ticks[i * 2]) & mask) * timestampPeriod_ / 1e6;
    if (!captureOnly_)
    {
        timing_.Record(options_.benchmark);
        if (auto capture = panel_.TestCapture(); !capture.empty())
            OnScreenshot(capture);
    }
    if (capture_.buffer)
    {
        if (!capture_.coherent)
        {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = capture_.memory;
            range.size = VK_WHOLE_SIZE;
            CheckVk(vkInvalidateMappedMemoryRanges(device_, 1, &range), "Capture invalidate");
        }
        auto p = static_cast<uint8_t *>(capture_.mapped);
        if (colorFormat_ == VK_FORMAT_B8G8R8A8_SRGB)
            for (size_t i = 0; i < size_t(width_) * height_; ++i)
                std::swap(p[i * 4], p[i * 4 + 2]);
        SavePng(capturePath_, width_, height_, p, width_ * 4);
        DestroyBuffer(capture_);
        capturePath_.clear();
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquire == VK_SUBOPTIMAL_KHR)
        OnResize(width_, height_);
}
void Sample::DestroyPipelines()
{
    for (auto *p : {&raster_, &mesh_, &display_, &light_, &reset_, &cull_, &finalize_, &inlinePipeline_,
                    &countReset_, &countVisible_})
    {
        if (*p)
            vkDestroyPipeline(device_, *p, nullptr);
        *p = VK_NULL_HANDLE;
    }
    for (auto m : modules_)
        vkDestroyShaderModule(device_, m, nullptr);
    modules_.clear();
    if (pipelineLayout_)
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    pipelineLayout_ = VK_NULL_HANDLE;
    if (descriptorPool_)
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    descriptorPool_ = VK_NULL_HANDLE;
    descriptorSets_.clear();
    if (descriptorLayout_)
        vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);
    descriptorLayout_ = VK_NULL_HANDLE;
}
void Sample::DestroyScene()
{
    for (auto &a : blas_)
        DestroyAcceleration(a);
    blas_.clear();
    DestroyAcceleration(tlas_);

    for (auto &b : buffers_)
        DestroyBuffer(b);
    buffers_.clear();
    for (auto &t : textures_)
        DestroyImage(t);
    textures_.clear();
    DestroyBuffer(constants_);
    DestroyBuffer(visibility_);
    if (sampler_)
        vkDestroySampler(device_, sampler_, nullptr);
    sampler_ = VK_NULL_HANDLE;
}
void Sample::OnResize(uint32_t width, uint32_t height)
{
    if (!width || !height)
        return;
    CheckVk(vkDeviceWaitIdle(device_), "Resize completion");
    DestroySwapchain();
    width_ = width;
    height_ = height;
    CreateSwapchain();
    CreateSizeResources();
}
void Sample::OnScreenshot(const std::filesystem::path &path)
{
    capturePath_ = path;
    captureOnly_ = true;
    OnRender();
    if (!capturePath_.empty())
        OnRender();
    captureOnly_ = false;
    if (!capturePath_.empty())
        throw std::runtime_error("Capture failed after swapchain resize");
    if (options_.captureGpu)
        DumpGpuData(path);
}
void Sample::DumpGpuData(const std::filesystem::path &path)
{
    auto directory = path.parent_path() / (path.stem().wstring() + L"-gpu");
    std::filesystem::create_directories(directory);
    const char *names[] = {
        "hdr", GALLERY_TECHNIQUE == 14 ? "surface" : "base", "normal", "position", "emission", "depth"};
    for (uint32_t i = 0; i < (GALLERY_TECHNIQUE == 14 ? 2u : 6u); ++i)
    {
        const auto aspect = i == 5 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        const auto image = i == 5 ? depth_.image : hdr_[i].image;
        auto &layout = i == 5 ? depthLayout_ : hdrLayouts_[i];
        const VkDeviceSize bytes = VkDeviceSize(width_) * height_ * (i == 5 ? 4 : 16);
        auto readback = CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        BeginCommands();
        auto before = layout;
        Move(image, aspect, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VkImageAspectFlags(aspect), 0, 0, 1};
        copy.imageExtent = {width_, height_, 1};
        vkCmdCopyImageToBuffer(commands_, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1,
                               &copy);
        Move(image, aspect, layout, before);
        HostReadBarrier(commands_, readback.buffer);
        Submit();
        if (!readback.coherent)
        {
            VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            r.memory = readback.memory;
            r.size = VK_WHOLE_SIZE;
            CheckVk(vkInvalidateMappedMemoryRanges(device_, 1, &r), "HDR invalidate");
        }
        std::ofstream file(directory / (std::string(names[i]) + ".bin"), std::ios::binary);
        file.write(static_cast<const char *>(readback.mapped), std::streamsize(bytes));
        DestroyBuffer(readback);
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
void Sample::OnDestroy() noexcept
{
    if (device_)
    {
        vkDeviceWaitIdle(device_);
        try
        {
            if (timing_.frame && (!options_.timings.empty() || options_.captureGpu))
                CollectStatistics();
            timing_.Save(options_, data_, panel_);
        }
        catch (const std::exception &e)
        {
            ValidationError(e.what());
        }
        DestroyImGui();
        DestroyPipelines();
        DestroyScene();
        for (auto &i : hdr_)
            DestroyImage(i);
        DestroyImage(depth_);
        DestroyBuffer(capture_);
        DestroySwapchain();
        if (timestamps_)
            vkDestroyQueryPool(device_, timestamps_, nullptr);
        if (fence_)
            vkDestroyFence(device_, fence_, nullptr);
        if (imageAvailable_)
            vkDestroySemaphore(device_, imageAvailable_, nullptr);
        if (commandPool_)
            vkDestroyCommandPool(device_, commandPool_, nullptr);
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_)
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (debug_)
    {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy)
            destroy(instance_, debug_, nullptr);
    }
    if (instance_)
        vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
}

// Win32 swapchain, explicit allocation and synchronization follow the earlier samples.
uint32_t Sample::MemoryType(uint32_t bits, VkMemoryPropertyFlags required)
{
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory);
    for (uint32_t index = 0; index < memory.memoryTypeCount; ++index)
        if ((bits & (1u << index)) && (memory.memoryTypes[index].propertyFlags & required) == required)
            return index;
    throw std::runtime_error("No compatible Vulkan memory type");
}

Sample::Buffer Sample::CreateBuffer(VkDeviceSize bytes, VkBufferUsageFlags usage, const void *data)
{
    Buffer buffer;
    try
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = bytes;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckVk(vkCreateBuffer(device_, &info, nullptr, &buffer.buffer), "vkCreateBuffer");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, buffer.buffer, &requirements);
        uint32_t type;
        try
        {
            type = MemoryType(requirements.memoryTypeBits,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            buffer.coherent = true;
        }
        catch (const std::exception &)
        {
            type = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        }
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
            allocation.pNext = &flags;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        CheckVk(vkAllocateMemory(device_, &allocation, nullptr, &buffer.memory), "Buffer memory allocation");
        CheckVk(vkBindBufferMemory(device_, buffer.buffer, buffer.memory, 0), "vkBindBufferMemory");
        CheckVk(vkMapMemory(device_, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped), "vkMapMemory");
        if (data)
        {
            std::memcpy(buffer.mapped, data, static_cast<size_t>(bytes));
            Flush(buffer);
        }
        return buffer;
    }
    catch (...)
    {
        DestroyBuffer(buffer);
        throw;
    }
}

void Sample::Flush(const Buffer &buffer)
{
    if (!buffer.coherent)
    {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = buffer.memory;
        range.size = VK_WHOLE_SIZE;
        CheckVk(vkFlushMappedMemoryRanges(device_, 1, &range), "vkFlushMappedMemoryRanges");
    }
}

void Sample::DestroyBuffer(Buffer &buffer)
{
    if (buffer.mapped)
        vkUnmapMemory(device_, buffer.memory);
    if (buffer.buffer)
        vkDestroyBuffer(device_, buffer.buffer, nullptr);
    if (buffer.memory)
        vkFreeMemory(device_, buffer.memory, nullptr);
    buffer = {};
}

void Sample::DestroyImage(ImageResource &image)
{
    if (image.view)
        vkDestroyImageView(device_, image.view, nullptr);
    if (image.image)
        vkDestroyImage(device_, image.image, nullptr);
    if (image.memory)
        vkFreeMemory(device_, image.memory, nullptr);
    image = {};
}

void Sample::DestroySwapchain()
{

    for (auto view : views_)
        if (view)
            vkDestroyImageView(device_, view, nullptr);
    for (auto semaphore : renderFinished_)
        if (semaphore)
            vkDestroySemaphore(device_, semaphore, nullptr);
    views_.clear();
    renderFinished_.clear();
    images_.clear();
    presented_.clear();
    if (swapchain_)
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
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

Sample::ImageResource Sample::CreateImage(uint32_t width, uint32_t height, VkFormat format,
                                          VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t mips,
                                          uint32_t layers, bool array)
{
    ImageResource resource;
    try
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {width, height, 1};
        info.mipLevels = mips;
        info.arrayLayers = layers;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckVk(vkCreateImage(device_, &info, nullptr, &resource.image), "vkCreateImage");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, resource.image, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex =
            MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CheckVk(vkAllocateMemory(device_, &allocation, nullptr, &resource.memory), "Image memory allocation");
        CheckVk(vkBindImageMemory(device_, resource.image, resource.memory, 0), "vkBindImageMemory");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = resource.image;
        view.viewType = array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {aspect, 0, mips, 0, layers};
        CheckVk(vkCreateImageView(device_, &view, nullptr, &resource.view), "vkCreateImageView");
        return resource;
    }
    catch (...)
    {
        DestroyImage(resource);
        throw;
    }
}

void Sample::CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities{};
    CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &capabilities),
            "Surface capabilities");
    uint32_t formatCount = 0;
    CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, nullptr),
            "Surface formats");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, formats.data()),
            "Surface formats");
    VkSurfaceFormatKHR selected{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    for (auto preferred : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB})
    {
        for (const auto &format : formats)
            if ((format.format == preferred || format.format == VK_FORMAT_UNDEFINED) &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                selected = {preferred, format.colorSpace};
                break;
            }
        if (selected.format != VK_FORMAT_UNDEFINED)
            break;
    }
    if (selected.format == VK_FORMAT_UNDEFINED)
        throw std::runtime_error("An RGBA8/BGRA8 sRGB swapchain format is required");
    colorFormat_ = selected.format;
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX)
        extent = {
            std::clamp(width_, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(height_, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};
    width_ = extent.width;
    height_ = extent.height;
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount)
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = colorFormat_;
    info.imageColorSpace = selected.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    canCapture_ = (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (canCapture_)
        info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    for (auto alpha : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                       VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
        if (capabilities.supportedCompositeAlpha & alpha)
        {
            info.compositeAlpha = alpha;
            break;
        }
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (options_.benchmark)
    {
        uint32_t count = 0;
        CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &count, nullptr),
                "Present modes");
        std::vector<VkPresentModeKHR> modes(count);
        CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &count, modes.data()),
                "Present modes");
        if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end())
            info.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        else
            throw std::runtime_error("Benchmark requires immediate present mode");
    }
    timing_.presentMode = info.presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "immediate" : "fifo";
    info.clipped = VK_TRUE;
    CheckVk(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "vkCreateSwapchainKHR");
    CheckVk(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr), "Swapchain images");
    images_.resize(imageCount);
    CheckVk(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, images_.data()), "Swapchain images");
    views_.resize(imageCount);
    renderFinished_.resize(imageCount);
    presented_.assign(imageCount, false);
    for (uint32_t index = 0; index < imageCount; ++index)
    {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = images_[index];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = colorFormat_;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        CheckVk(vkCreateImageView(device_, &view, nullptr, &views_[index]), "Swapchain image view");
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        CheckVk(vkCreateSemaphore(device_, &semaphore, nullptr, &renderFinished_[index]),
                "Present semaphore");
    }
}

void Sample::Submit()
{
    CheckVk(vkEndCommandBuffer(commands_), "Command end");
    VkCommandBufferSubmitInfo c{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    c.commandBuffer = commands_;
    VkSubmitInfo2 s{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    s.commandBufferInfoCount = 1;
    s.pCommandBufferInfos = &c;
    CheckVk(vkQueueSubmit2(queue_, 1, &s, VK_NULL_HANDLE), "Immediate submit");
    CheckVk(vkQueueWaitIdle(queue_), "Immediate completion");
}

Sample::Buffer Sample::DeviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
    Buffer b;
    try
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage =
            usage | (GALLERY_TECHNIQUE >= 14 ? uint32_t(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) : 0u);
        CheckVk(vkCreateBuffer(device_, &info, nullptr, &b.buffer), "AS buffer");
        VkMemoryRequirements r{};
        vkGetBufferMemoryRequirements(device_, b.buffer, &r);
        VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        flags.flags = GALLERY_TECHNIQUE >= 14 ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        a.pNext = &flags;
        a.allocationSize = r.size;
        a.memoryTypeIndex = MemoryType(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CheckVk(vkAllocateMemory(device_, &a, nullptr, &b.memory), "AS memory");
        CheckVk(vkBindBufferMemory(device_, b.buffer, b.memory, 0), "AS bind");
        return b;
    }
    catch (...)
    {
        DestroyBuffer(b);
        throw;
    }
}

void Sample::DestroyAcceleration(Acceleration &a)
{
    if (a.object)
        destroyAS_(device_, a.object, nullptr);
    a.object = VK_NULL_HANDLE;
    DestroyBuffer(a.storage);
}

void Sample::Begin(const std::vector<VkImageView> &colors, VkImageView depth, bool clear, bool readOnly,
                   uint32_t width, uint32_t height)
{
    std::vector<VkRenderingAttachmentInfo> a;
    for (auto view : colors)
    {
        VkRenderingAttachmentInfo v{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        v.imageView = view;
        v.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        v.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        v.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.push_back(v);
    }
    VkRenderingAttachmentInfo d{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    d.imageView = depth;
    d.imageLayout = readOnly ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                             : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    d.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    d.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    d.clearValue.depthStencil = {1, 0};
    VkRenderingInfo r{VK_STRUCTURE_TYPE_RENDERING_INFO};
    r.renderArea = {{0, 0}, {width, height}};
    r.layerCount = 1;
    r.colorAttachmentCount = uint32_t(a.size());
    r.pColorAttachments = a.data();
    if (depth)
        r.pDepthAttachment = &d;
    vkCmdBeginRendering(commands_, &r);
    VkViewport vp{0, float(height), float(width), -float(height), 0, 1};
    VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(commands_, 0, 1, &vp);
    vkCmdSetScissor(commands_, 0, 1, &scissor);
}

void Sample::CollectStatistics()
{
    const auto savedBinds = timing_.binds;
    auto readback = CreateBuffer(516, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    BeginCommands();
    Barrier();
    Bind(VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, countReset_);
    vkCmdDispatch(commands_, 1, 1, 1);
    Barrier();
    vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, countVisible_);
    vkCmdDispatch(commands_, (width_ + 7) / 8, (height_ + 7) / 8, 1);
    Barrier();
    VkBufferCopy copies[] = {{32, 0, 512}, {0, 512, 4}};
    vkCmdCopyBuffer(commands_, visibility_.buffer, readback.buffer, 2, copies);
    HostReadBarrier(commands_, readback.buffer);
    Submit();
    if (!readback.coherent)
    {
        VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        r.memory = readback.memory;
        r.size = VK_WHOLE_SIZE;
        CheckVk(vkInvalidateMappedMemoryRanges(device_, 1, &r), "Statistics invalidate");
    }
    auto *words = static_cast<const uint32_t *>(readback.mapped);
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
    auto reference = GalleryFrustumCandidates(data_, frame_);
    timing_.frustumInstances = reference[0];
    timing_.frustumMeshlets = reference[1];
    DestroyBuffer(readback);
    timing_.binds = savedBinds;
}

void Sample::CreateInlinePipeline()
{
    auto bytes = ReadBinary(AssetPath(std::filesystem::path(L"Shaders") /
                                      (L"InlineHybrid" + std::to_wstring(panel_.binding) + L".spv")));
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytes.size();
    info.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
    VkShaderModule module;
    CheckVk(vkCreateShaderModule(device_, &info, nullptr, &module), "Inline shader module");
    modules_.push_back(module);
    VkComputePipelineCreateInfo d{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    d.layout = pipelineLayout_;
    d.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    d.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    d.stage.module = module;
    d.stage.pName = "CSMain";
    CheckVk(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &d, nullptr, &inlinePipeline_),
            "Inline Hybrid CS pipeline");
}
