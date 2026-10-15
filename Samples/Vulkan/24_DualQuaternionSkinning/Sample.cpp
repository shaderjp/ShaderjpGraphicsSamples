#include "Sample.h"
#include "../../Shared/MeshFixtures.h"
#include "../../Shared/PostProcessCapture.h"
#include <imgui_impl_win32.h>
// Declared explicitly by the platform backend to avoid pulling Windows headers into imgui.h.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
using namespace tutorial;
namespace
{
void CheckVk(VkResult result, const char *operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(result) +
                                 ')');
}
VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                            VkDebugUtilsMessageTypeFlagsEXT,
                                            const VkDebugUtilsMessengerCallbackDataEXT *data, void *)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        ValidationError(data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        Trace(std::string("Vulkan warning: ") + data->pMessage);
    return VK_FALSE;
}
void ImageBarrier(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect, VkImageLayout before,
                  VkImageLayout after, VkPipelineStageFlags2 sourceStage, VkAccessFlags2 sourceAccess,
                  VkPipelineStageFlags2 destinationStage, VkAccessFlags2 destinationAccess)
{
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = sourceStage;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstStageMask = destinationStage;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = before;
    barrier.newLayout = after;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
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
    timing_.api = "Vulkan";
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
    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
                                VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
#ifdef _DEBUG
    const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
#endif
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Shader.jp Vulkan Tutorial";
    application.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &application;
    instanceInfo.enabledExtensionCount = 2;
    instanceInfo.ppEnabledExtensionNames = extensions;
    VkDebugUtilsMessengerCreateInfoEXT debugInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debugInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = DebugMessage;
#ifdef _DEBUG
    uint32_t layerCount = 0;
    CheckVk(vkEnumerateInstanceLayerProperties(&layerCount, nullptr), "Enumerate layers");
    std::vector<VkLayerProperties> available(layerCount);
    CheckVk(vkEnumerateInstanceLayerProperties(&layerCount, available.data()), "Enumerate layers");
    if (std::none_of(available.begin(), available.end(), [](const auto &layer)
                     { return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0; }))
        throw std::runtime_error("Install Vulkan SDK validation layers for Debug builds");
    instanceInfo.enabledLayerCount = 1;
    instanceInfo.ppEnabledLayerNames = layers;
    instanceInfo.enabledExtensionCount = 3;
    instanceInfo.pNext = &debugInfo;
#endif
    CheckVk(vkCreateInstance(&instanceInfo, nullptr, &instance_),
            "vkCreateInstance (Vulkan 1.3 loader required)");
#ifdef _DEBUG
    auto createDebug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (!createDebug)
        throw std::runtime_error("VK_EXT_debug_utils is unavailable");
    CheckVk(createDebug(instance_, &debugInfo, nullptr, &debug_), "Create debug messenger");
    Trace("Vulkan Validation Layer enabled");
#endif
    VkWin32SurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    surfaceInfo.hinstance = GetModuleHandleW(nullptr);
    surfaceInfo.hwnd = window;
    CheckVk(vkCreateWin32SurfaceKHR(instance_, &surfaceInfo, nullptr, &surface_), "vkCreateWin32SurfaceKHR");
    uint32_t deviceCount = 0;
    CheckVk(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr), "Enumerate GPUs");
    std::vector<VkPhysicalDevice> devices(deviceCount);
    CheckVk(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()), "Enumerate GPUs");
    int bestScore = -1;
    for (auto candidate : devices)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3)
            continue;
        VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        supported.pNext = &features;
        vkGetPhysicalDeviceFeatures2(candidate, &supported);
        if (!features.dynamicRendering || !features.synchronization2 ||
            !features.shaderDemoteToHelperInvocation ||
            (POST_TECHNIQUE >= 15 && !supported.features.shaderStorageImageExtendedFormats))
            continue;
        uint32_t extensionCount = 0;
        CheckVk(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr),
                "Device extensions");
        std::vector<VkExtensionProperties> deviceExtensions(extensionCount);
        CheckVk(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount,
                                                     deviceExtensions.data()),
                "Device extensions");
        if (std::none_of(
                deviceExtensions.begin(), deviceExtensions.end(), [](const auto &extension)
                { return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; }))
            continue;
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (uint32_t family = 0; family < familyCount; ++family)
        {
            VkBool32 present = VK_FALSE;
            CheckVk(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, surface_, &present),
                    "Surface queue support");
            const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if ((families[family].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                    (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT) &&
                present && score > bestScore)
            {
                physical_ = candidate;
                queueFamily_ = family;
                bestScore = score;
            }
        }
    }
    if (!physical_)
        throw std::runtime_error(
            "Vulkan 1.3, dynamicRendering, synchronization2, and a graphics+present queue are required");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_, &properties);
    Trace(std::string("GPU: ") + properties.deviceName);
    const float priority = 1;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    const char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_MESH_SHADER_EXTENSION_NAME};
    VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features.dynamicRendering = VK_TRUE;
    features.synchronization2 = VK_TRUE;
    features.shaderDemoteToHelperInvocation = VK_TRUE;

    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    VkPhysicalDeviceVulkan12Features descriptorFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 meshSupported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    meshSupported.pNext = &descriptorFeatures;
    descriptorFeatures.pNext = SKIN_TECHNIQUE == 3 ? &meshFeatures : nullptr;
    vkGetPhysicalDeviceFeatures2(physical_, &meshSupported);
    if ((SKIN_TECHNIQUE == 3 && (!meshFeatures.meshShader || !meshFeatures.taskShader)) ||
        !descriptorFeatures.shaderSampledImageArrayNonUniformIndexing ||
        !meshSupported.features.vertexPipelineStoresAndAtomics)
        throw std::runtime_error("VK_EXT_mesh_shader, meshShader / taskShader, non-uniform textures and "
                                 "vertexPipelineStoresAndAtomics are required");
    if constexpr (SKIN_TECHNIQUE == 3)
    {
        VkPhysicalDeviceMeshShaderPropertiesEXT meshProperties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 meshPhysical{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        meshPhysical.pNext = &meshProperties;
        vkGetPhysicalDeviceProperties2(physical_, &meshPhysical);
        if (meshProperties.maxMeshOutputVertices < 96 || meshProperties.maxMeshOutputPrimitives < 96 ||
            meshProperties.maxMeshWorkGroupInvocations < 64 ||
            (MESH_TECHNIQUE >= 6 &&
             (meshProperties.maxTaskWorkGroupInvocations < 32 || meshProperties.maxTaskPayloadSize < 128)) ||
            properties.limits.maxPerStageDescriptorSampledImages < 78 ||
            properties.limits.maxPerStageDescriptorStorageBuffers < 18)
            throw std::runtime_error("Mesh Shader output, payload or descriptor limits are insufficient");
        meshDispatchWidth_ = std::min(256u, meshProperties.maxMeshWorkGroupCount[0]);
        Trace("Mesh limits: vertices=" + std::to_string(meshProperties.maxMeshOutputVertices) +
              ", primitives=" + std::to_string(meshProperties.maxMeshOutputPrimitives) +
              ", payload=" + std::to_string(meshProperties.maxTaskPayloadSize) +
              ", mesh invocations=" + std::to_string(meshProperties.maxMeshWorkGroupInvocations));
    }
    meshFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    meshFeatures.meshShader = SKIN_TECHNIQUE == 3 ? VK_TRUE : VK_FALSE;
    meshFeatures.taskShader = MESH_TECHNIQUE >= 6 ? VK_TRUE : VK_FALSE;
    descriptorFeatures = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    descriptorFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    descriptorFeatures.pNext = SKIN_TECHNIQUE == 3 ? &meshFeatures : nullptr;
    features.pNext = &descriptorFeatures;
    if (MESH_TECHNIQUE >= 6)
        meshStageFlags_ |= VK_SHADER_STAGE_TASK_BIT_EXT;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkPhysicalDeviceFeatures basic{};
    basic.shaderStorageImageExtendedFormats = POST_TECHNIQUE >= 15 ? VK_TRUE : VK_FALSE;
    basic.vertexPipelineStoresAndAtomics = VK_TRUE;
    deviceInfo.pEnabledFeatures = &basic;
    deviceInfo.pNext = &features;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = SKIN_TECHNIQUE == 3 ? 2 : 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;
    CheckVk(vkCreateDevice(physical_, &deviceInfo, nullptr, &device_), "vkCreateDevice");
    drawMesh_ =
        reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(vkGetDeviceProcAddr(device_, "vkCmdDrawMeshTasksEXT"));
    drawMeshIndirect_ = reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectEXT>(
        vkGetDeviceProcAddr(device_, "vkCmdDrawMeshTasksIndirectEXT"));
    if (SKIN_TECHNIQUE == 3 && (!drawMesh_ || !drawMeshIndirect_))
        throw std::runtime_error("EXT Mesh Shader commands unavailable");
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = queueFamily_;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    CheckVk(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = commandPool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    CheckVk(vkAllocateCommandBuffers(device_, &allocate, &commands_), "vkAllocateCommandBuffers");
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    CheckVk(vkCreateFence(device_, &fenceInfo, nullptr, &fence_), "vkCreateFence");
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    CheckVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailable_), "Acquire semaphore");
    timing_.gpu = properties.deviceName;
    timestampPeriod_ = properties.limits.timestampPeriod;
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &familyCount, families.data());
    timestampBits_ = families[queueFamily_].timestampValidBits;
    if (!timestampBits_ && options.benchmark)
        throw std::runtime_error("GPU timestamps unsupported on this queue");
    if (timestampBits_)
    {
        VkQueryPoolCreateInfo q{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        q.queryType = VK_QUERY_TYPE_TIMESTAMP;
        q.queryCount = 26;
        CheckVk(vkCreateQueryPool(device_, &q, nullptr, &queries_), "Timestamp queries");
    }
    constantStride_ = (sizeof(DeferredConstants) + properties.limits.minUniformBufferOffsetAlignment - 1) &
                      ~(properties.limits.minUniformBufferOffsetAlignment - 1);
    CreateSwapchain();
    CreateScene();
    CreateSizeResources();
    CreatePipeline();
    CreateMeshPipeline();

    CreateImGui();
}
uint32_t Sample::MemoryType(uint32_t bits, VkMemoryPropertyFlags required)
{
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory);
    for (uint32_t index = 0; index < memory.memoryTypeCount; ++index)
        if ((bits & (1u << index)) && (memory.memoryTypes[index].propertyFlags & required) == required)
            return index;
    throw std::runtime_error("No compatible Vulkan memory type");
}
Sample::Buffer Sample::CreateBuffer(VkDeviceSize bytes, VkBufferUsageFlags usage, const void *data,
                                    bool local)
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
        if (local)
            type = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        else
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
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        CheckVk(vkAllocateMemory(device_, &allocation, nullptr, &buffer.memory), "Buffer memory allocation");
        CheckVk(vkBindBufferMemory(device_, buffer.buffer, buffer.memory, 0), "vkBindBufferMemory");
        if (!local)
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
                                          VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t mips)
{
    ImageResource resource;
    try
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {width, height, 1};
        info.mipLevels = mips;
        info.arrayLayers = 1;
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
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {aspect, 0, mips, 0, 1};
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
Sample::ImageResource Sample::LoadDdsTexture(const std::filesystem::path &path)
{
    auto d = LoadDds(path);
    ImageResource image;
    VkFormat format =
        d.format == DdsFormat::Rg16Float ? VK_FORMAT_R16G16_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormatProperties supported{};
    vkGetPhysicalDeviceFormatProperties(physical_, format, &supported);
    const auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                          VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((supported.optimalTilingFeatures & required) != required)
        throw std::runtime_error("HDR image format unsupported");
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags = d.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {d.width, d.height, 1};
    info.mipLevels = d.mips;
    info.arrayLayers = d.faces;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    CheckVk(vkCreateImage(device_, &info, nullptr, &image.image), "DDS image");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, image.image, &requirements);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CheckVk(vkAllocateMemory(device_, &alloc, nullptr, &image.memory), "DDS image memory");
    CheckVk(vkBindImageMemory(device_, image.image, image.memory, 0), "DDS image bind");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image.image;
    view.viewType = d.cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, d.mips, 0, d.faces};
    CheckVk(vkCreateImageView(device_, &view, nullptr, &image.view), "DDS image view");
    std::vector<uint8_t> bytes;
    std::vector<VkBufferImageCopy> regions;
    for (const auto &sub : d.subresources)
    {
        VkBufferImageCopy copy{};
        copy.bufferOffset = bytes.size();
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, sub.mip, sub.face, 1};
        copy.imageExtent = {sub.width, sub.height, 1};
        regions.push_back(copy);
        bytes.insert(bytes.end(), sub.bytes.begin(), sub.bytes.end());
    }
    staging_ = CreateBuffer(bytes.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, bytes.data());
    CheckVk(vkResetCommandBuffer(commands_, 0), "DDS reset");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(commands_, &begin), "DDS begin");
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.image = image.image;
    b.subresourceRange = view.subresourceRange;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(commands_, &dependency);
    vkCmdCopyBufferToImage(commands_, staging_.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    vkCmdPipelineBarrier2(commands_, &dependency);
    CheckVk(vkEndCommandBuffer(commands_), "DDS end");
    VkCommandBufferSubmitInfo cmd{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd.commandBuffer = commands_;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd;
    CheckVk(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE), "DDS submit");
    CheckVk(vkQueueWaitIdle(queue_), "DDS completion");
    DestroyBuffer(staging_);
    return image;
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
void Sample::Move(VkImage image, VkImageAspectFlags aspect, VkImageLayout &layout, VkImageLayout next)
{
    auto stage = [](VkImageLayout l)
    {
        switch (l)
        {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return VkPipelineStageFlags2(VK_PIPELINE_STAGE_2_NONE);
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VkPipelineStageFlags2(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VkPipelineStageFlags2(VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                         VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return VkPipelineStageFlags2(
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VkPipelineStageFlags2(VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        case VK_IMAGE_LAYOUT_GENERAL:
            return VkPipelineStageFlags2(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        default:
            return VkPipelineStageFlags2(VK_PIPELINE_STAGE_2_COPY_BIT);
        }
    };
    auto access = [](VkImageLayout l)
    {
        switch (l)
        {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return VkAccessFlags2(0);
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VkAccessFlags2(VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VkAccessFlags2(VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return VkAccessFlags2(VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VkAccessFlags2(VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        case VK_IMAGE_LAYOUT_GENERAL:
            return VkAccessFlags2(VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        default:
            return VkAccessFlags2(VK_ACCESS_2_TRANSFER_READ_BIT);
        }
    };
    ImageBarrier(commands_, image, aspect, layout, next, stage(layout), access(layout), stage(next),
                 access(next));
    layout = next;
}
void Sample::CreateScene()
{
    for (const auto &p : workshop_.primitives)
    {
        Geometry g;
        g.vertices = CreateBuffer(p.vertices.size() * sizeof(PbrVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  p.vertices.data());
        g.indices = CreateBuffer(p.indices.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, p.indices.data());
        g.count = uint32_t(p.indices.size());
        geometry_.push_back(g);
    }
    CreateMeshResources();
    for (const auto &im : workshop_.images)
    {
        auto mips = GenerateMipChain(MeshTextureImage(im), im.semantic, im.alphaCutoff);
        auto image = CreateImage(mips[0].width, mips[0].height,
                                 im.semantic == ImageSemantic::Color ? VK_FORMAT_R8G8B8A8_SRGB
                                                                     : VK_FORMAT_R8G8B8A8_UNORM,
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(mips.size()));
        std::vector<uint8_t> bytes;
        std::vector<VkBufferImageCopy> copies;
        for (uint32_t m = 0; m < mips.size(); ++m)
        {
            VkBufferImageCopy c{};
            c.bufferOffset = bytes.size();
            c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1};
            c.imageExtent = {mips[m].width, mips[m].height, 1};
            copies.push_back(c);
            bytes.insert(bytes.end(), mips[m].rgba.begin(), mips[m].rgba.end());
        }
        auto upload = CreateBuffer(bytes.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, bytes.data());
        CheckVk(vkResetCommandBuffer(commands_, 0), "PNG reset");
        VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVk(vkBeginCommandBuffer(commands_, &b), "PNG begin");
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.image = image.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, uint32_t(mips.size()), 0, 1};
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.imageMemoryBarrierCount = 1;
        d.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commands_, &d);
        vkCmdCopyBufferToImage(commands_, upload.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               uint32_t(copies.size()), copies.data());
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask =
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        vkCmdPipelineBarrier2(commands_, &d);
        Submit();
        DestroyBuffer(upload);
        textures_.push_back(image);
    }
    const wchar_t *suffix[] = {L"_Environment.dds", L"_Irradiance.dds", L"_Specular.dds"};
    for (int i = 0; i < 3; ++i)
        environment_[i] = LoadDdsTexture(workshop_.environment.wstring() + suffix[i]);
    brdf_ = LoadDdsTexture(workshop_.environment.parent_path() / L"BRDF.dds");
    constants_ = CreateBuffer(1810 * constantStride_, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    postConstants_ = CreateBuffer(4 * constantStride_, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    points_ = CreateBuffer(MaximumPointLights * sizeof(WorkshopLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    gridConstants_ = CreateBuffer(sizeof(LightGridConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    s.magFilter = s.minFilter = VK_FILTER_LINEAR;
    s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    s.maxLod = VK_LOD_CLAMP_NONE;
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    CheckVk(vkCreateSampler(device_, &s, nullptr, &sampler_), "Material sampler");
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    CheckVk(vkCreateSampler(device_, &s, nullptr, &environmentSampler_), "Environment sampler");
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (uint32_t b = 0; b < 24; ++b)
    {
        VkDescriptorType type = b == 0               ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                : b == 2 || b == 11  ? VK_DESCRIPTOR_TYPE_SAMPLER
                                : b == 19            ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                : b == 12 || b >= 20 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                : b == 18            ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                     : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings.push_back({b, type, 1, meshStageFlags_, nullptr});
    }
    VkDescriptorSetLayoutCreateInfo l{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    for (uint32_t b = 24; b < 42; ++b)
        bindings.push_back({b, b == 36 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            b == 36 ? 64u : 1u, meshStageFlags_, nullptr});
    l.bindingCount = uint32_t(bindings.size());
    l.pBindings = bindings.data();
    CheckVk(vkCreateDescriptorSetLayout(device_, &l, nullptr, &descriptorLayout_), "Scene descriptor layout");
    uint32_t count = uint32_t(workshop_.materials.size());
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, count}, {VK_DESCRIPTOR_TYPE_SAMPLER, count * 2},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, count * 22},    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, count},          {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, count * 78}};
    VkDescriptorPoolCreateInfo p{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    p.maxSets = count;
    p.poolSizeCount = 6;
    p.pPoolSizes = sizes;
    CheckVk(vkCreateDescriptorPool(device_, &p, nullptr, &descriptorPool_), "Scene pool");
    std::vector<VkDescriptorSetLayout> layouts(count, descriptorLayout_);
    descriptorSets_.resize(count);
    VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    a.descriptorPool = descriptorPool_;
    a.descriptorSetCount = count;
    a.pSetLayouts = layouts.data();
    CheckVk(vkAllocateDescriptorSets(device_, &a, descriptorSets_.data()), "Scene sets");
    const VkDescriptorSetLayoutBinding post[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
         nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
         nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
         nullptr},
        {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
         nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    l.bindingCount = 5;
    l.pBindings = post;
    CheckVk(vkCreateDescriptorSetLayout(device_, &l, nullptr, &postDescriptorLayout_), "Post layout");
    const VkDescriptorPoolSize ps[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4},
                                       {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8},
                                       {VK_DESCRIPTOR_TYPE_SAMPLER, 4},
                                       {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4}};
    p.maxSets = 4;
    p.poolSizeCount = 4;
    p.pPoolSizes = ps;
    CheckVk(vkCreateDescriptorPool(device_, &p, nullptr, &postPool_), "Post pool");
    std::array<VkDescriptorSetLayout, 4> pl;
    pl.fill(postDescriptorLayout_);
    a.descriptorPool = postPool_;
    a.descriptorSetCount = 4;
    a.pSetLayouts = pl.data();
    CheckVk(vkAllocateDescriptorSets(device_, &a, postSets_.data()), "Post sets");
    VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pi.setLayoutCount = 1;
    VkPushConstantRange push{meshStageFlags_, 0, 16};
    pi.pushConstantRangeCount = 1;
    pi.pPushConstantRanges = &push;
    pi.pSetLayouts = &descriptorLayout_;
    CheckVk(vkCreatePipelineLayout(device_, &pi, nullptr, &pipelineLayout_), "Scene pipeline layout");
    pi.pushConstantRangeCount = 0;
    pi.pPushConstantRanges = nullptr;
    pi.pSetLayouts = &postDescriptorLayout_;
    CheckVk(vkCreatePipelineLayout(device_, &pi, nullptr, &postLayout_), "Post pipeline layout");
}
void Sample::CreateSizeResources()
{
    VkFormatProperties hdrSupport{};
    vkGetPhysicalDeviceFormatProperties(physical_, VK_FORMAT_R16G16B16A16_SFLOAT, &hdrSupport);
    const VkFormatFeatureFlags hdrRequired =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if ((hdrSupport.optimalTilingFeatures & hdrRequired) != hdrRequired)
        throw std::runtime_error("RGBA16F color/sampled/storage/transfer format unsupported");
    const VkFormat f[] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R16G16_SNORM, VK_FORMAT_R8G8_UNORM,
                          VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_FORMAT_D32_SFLOAT};
    for (uint32_t i = 0; i < 5; ++i)
    {
        VkFormatProperties p{};
        vkGetPhysicalDeviceFormatProperties(physical_, f[i], &p);
        VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                        (i == 4 ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
                                                : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
        if ((p.optimalTilingFeatures & required) != required)
            throw std::runtime_error("Required compact G-buffer format unsupported");
        gbuffer_[i] = CreateImage(
            width_, height_, f[i],
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                (i == 4 ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
            i == 4 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
        gLayouts_[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    for (uint32_t i = 0; i < 4; ++i)
    {
        hdr_[i] = CreateImage(i ? std::max(1u, (width_ + 1) / 2) : width_,
                              i ? std::max(1u, (height_ + 1) / 2) : height_, VK_FORMAT_R16G16B16A16_SFLOAT,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT);
        hdrLayouts_[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    if (!shadow_.image)
        shadow_ = CreateImage(2048, 2048, VK_FORMAT_D32_SFLOAT,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              VK_IMAGE_ASPECT_DEPTH_BIT);
    CreateLightGridResources();
    UpdateDescriptors();
}
void Sample::UpdateDescriptors()
{
    for (uint32_t m = 0; m < descriptorSets_.size(); ++m)
    {
        std::array<VkDescriptorImageInfo, 24> im{};
        std::array<VkDescriptorBufferInfo, 24> buf{};
        std::vector<VkWriteDescriptorSet> writes;
        for (uint32_t b = 0; b < 24; ++b)
        {
            VkWriteDescriptorSet d{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            d.dstSet = descriptorSets_[m];
            d.dstBinding = b;
            d.descriptorCount = 1;
            if (b >= 19)
            {
                d.descriptorType =
                    b == 19 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                buf[b] = {b == 19 ? gridConstants_.buffer : gridBuffers_[b - 20].buffer, 0,
                          b == 19 ? sizeof(LightGridConstants)
                                  : VkDeviceSize(b == 20   ? gridCells_ * 8ull
                                                 : b == 21 ? gridCells_ * LightGridStride * 4ull
                                                 : b == 22
                                                     ? ((width_ + 15) / 16) * ((height_ + 15) / 16) * 8ull
                                                     : ((width_ + 15) / 16) * ((height_ + 15) / 16) * 4ull)};
                d.pBufferInfo = &buf[b];
            }
            else if (b == 0 || b == 12)
            {
                d.descriptorType =
                    b == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                buf[b] = {b == 0 ? constants_.buffer : points_.buffer, 0,
                          b == 0 ? sizeof(DeferredConstants) : MaximumPointLights * sizeof(WorkshopLight)};
                d.pBufferInfo = &buf[b];
            }
            else
            {
                if (b == 2 || b == 11)
                {
                    d.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                    im[b].sampler = b == 2 ? sampler_ : environmentSampler_;
                }
                else if (b == 18)
                {
                    d.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    im[b].imageView = hdr_[0].view;
                    im[b].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                }
                else
                {
                    d.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    im[b].imageLayout = b == 10 || b == 17 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    if (b == 1)
                        im[b].imageView = textures_[workshop_.materials[m].images[0]].view;
                    else if (b >= 3 && b <= 5)
                        im[b].imageView = textures_[workshop_.materials[m].images[b - 2]].view;
                    else if (b >= 6 && b <= 8)
                        im[b].imageView = environment_[b - 6].view;
                    else if (b == 9)
                        im[b].imageView = brdf_.view;
                    else if (b == 10)
                        im[b].imageView = shadow_.view;
                    else
                        im[b].imageView = gbuffer_[b - 13].view;
                }
                d.pImageInfo = &im[b];
            }
            writes.push_back(d);
        }
        vkUpdateDescriptorSets(device_, uint32_t(writes.size()), writes.data(), 0, nullptr);
    }
    for (uint32_t p = 0; p < 4; ++p)
    {
        uint32_t source = p < 3 ? p : panel_.view >= 14 ? uint32_t(panel_.view - 13) : 0;
        VkDescriptorBufferInfo uniform{postConstants_.buffer, p * constantStride_, sizeof(PostConstants)};
        VkDescriptorImageInfo images[4] = {
            {VK_NULL_HANDLE, hdr_[source].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {environmentSampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
            {VK_NULL_HANDLE, hdr_[3].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, hdr_[p < 3 ? p + 1 : 0].view, VK_IMAGE_LAYOUT_GENERAL}};
        std::array<VkWriteDescriptorSet, 5> writes{};
        for (uint32_t b = 0; b < 5; ++b)
        {
            auto &d = writes[b];
            d.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            d.dstSet = postSets_[p];
            d.dstBinding = b;
            d.descriptorCount = 1;
            d.descriptorType = b == 0   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                               : b == 2 ? VK_DESCRIPTOR_TYPE_SAMPLER
                               : b == 4 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                        : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            if (!b)
                d.pBufferInfo = &uniform;
            else
                d.pImageInfo = &images[b - 1];
        }
        vkUpdateDescriptorSets(device_, 5, writes.data(), 0, nullptr);
    }
    UpdateMeshDescriptors();
}
void Sample::CreatePipeline()
{
    auto shader = [&](const wchar_t *name)
    {
        auto bytes =
            ReadBinary(AssetPath(std::filesystem::path(L"Shaders") / (std::wstring(name) + L".spv")));
        VkShaderModuleCreateInfo i{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        i.codeSize = bytes.size();
        i.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
        VkShaderModule module;
        CheckVk(vkCreateShaderModule(device_, &i, nullptr, &module), "Shader module");
        modules_.push_back(module);
        return module;
    };
    auto graphics = [&](VkPipeline &out, const wchar_t *vertex, const wchar_t *pixel, int kind, bool twoSided)
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (auto &s : stages)
            s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = shader(vertex);
        stages[0].pName = "VSMain";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = shader(pixel);
        stages[1].pName = "PSMain";
        VkVertexInputBindingDescription binding{0, sizeof(PbrVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const VkVertexInputAttributeDescription attrs[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                                           {1, 0, VK_FORMAT_R32G32_SFLOAT, 12},
                                                           {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 20},
                                                           {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32}};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        if ((kind <= 2 || kind == 6) && std::wstring(vertex) != L"ReferenceVertex")
        {
            vi.vertexBindingDescriptionCount = 1;
            vi.pVertexBindingDescriptions = &binding;
            vi.vertexAttributeDescriptionCount = 4;
            vi.pVertexAttributeDescriptions = attrs;
        }
        VkVertexInputAttributeDescription cacheAttrs[] = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                                                          {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
                                                          {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32}};
        if (std::wstring(vertex) == L"CacheVertex")
        {
            vi.vertexBindingDescriptionCount = 1;
            vi.pVertexBindingDescriptions = &binding;
            vi.vertexAttributeDescriptionCount = 3;
            vi.pVertexAttributeDescriptions = cacheAttrs;
        }
        VkPipelineInputAssemblyStateCreateInfo ia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = twoSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = kind <= 3 || kind == 6;
        ds.depthWriteEnable = kind <= 2;
        ds.depthCompareOp = kind == 3 ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS;
        std::array<VkPipelineColorBlendAttachmentState, 4> blend{};
        for (auto &b : blend)
            b.colorWriteMask = 15;
        if (kind == 6)
        {
            auto &b = blend[0];
            b.blendEnable = VK_TRUE;
            b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            b.colorBlendOp = b.alphaBlendOp = VK_BLEND_OP_ADD;
            b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        }
        VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        bs.attachmentCount = kind == 0 ? 4 : kind == 2 ? 0 : 1;
        bs.pAttachments = blend.data();
        const VkFormat formats[] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R16G16_SNORM, VK_FORMAT_R8G8_UNORM,
                                    VK_FORMAT_B10G11R11_UFLOAT_PACK32};
        VkFormat color = kind == 5 ? colorFormat_ : VK_FORMAT_R16G16B16A16_SFLOAT;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = bs.attachmentCount;
        rendering.pColorAttachmentFormats = kind == 0 ? formats : &color;
        rendering.depthAttachmentFormat = ds.depthTestEnable ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED;
        const VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dy.dynamicStateCount = 2;
        dy.pDynamicStates = dynamic;
        VkGraphicsPipelineCreateInfo p{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        p.pNext = &rendering;
        p.stageCount = 2;
        p.pStages = stages;
        p.pVertexInputState = &vi;
        p.pInputAssemblyState = &ia;
        p.pViewportState = &vp;
        p.pRasterizationState = &rs;
        p.pMultisampleState = &ms;
        p.pDepthStencilState = &ds;
        p.pColorBlendState = &bs;
        p.pDynamicState = &dy;
        p.layout = (kind == 5 || kind == 7) ? postLayout_ : pipelineLayout_;
        CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &p, nullptr, &out),
                "Deferred graphics pipeline");
    };
    for (int i = 0; i < 2; ++i)
    {
        graphics(gPipelines_[i], L"ReferenceVertex", L"MeshGBufferPixel", 0, i != 0);
        graphics(forwardPipelines_[i], L"ReferenceVertex", L"MeshForwardPixel", 1, i != 0);
        graphics(shadowPipelines_[i], L"ReferenceVertex", L"MeshShadowPixel", 2, i != 0);
    }
    graphics(skyPipeline_, L"SkyVertex", L"SkyPixel", 3, true);
    graphics(lightingPixel_, L"PostVertex", L"LightingPixel", 4, true);
    graphics(postPipeline_, L"PostVertex", L"DeferredPostPixel", 5, true);
    graphics(testPipeline_, L"PostVertex", L"TestPixel", 7, true);
    auto compute = [&](VkPipeline &out, const wchar_t *name, VkPipelineLayout layout)
    {
        VkComputePipelineCreateInfo p{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        p.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        p.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        p.stage.module = shader(name);
        p.stage.pName = "CSMain";
        p.layout = layout;
        CheckVk(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &p, nullptr, &out),
                "Deferred compute pipeline");
    };
    compute(lightingCompute_, L"LightingCompute", pipelineLayout_);
    compute(tileDepthPipeline_, L"TileDepth", pipelineLayout_);
    compute(cullPipeline_, L"CullLights", pipelineLayout_);
    compute(bloomPipelines_[0], L"Extract", postLayout_);
    compute(bloomPipelines_[1], L"Blur", postLayout_);
}
void Sample::DestroyPipelines()
{
    if (skinCompute_)
    {
        vkDestroyPipeline(device_, skinCompute_, nullptr);
        skinCompute_ = VK_NULL_HANDLE;
    }
    for (auto *group :
         {&gPipelines_, &forwardPipelines_, &transparentPipelines_, &shadowPipelines_, &bloomPipelines_})
        for (auto &p : *group)
        {
            if (p)
                vkDestroyPipeline(device_, p, nullptr);
            p = VK_NULL_HANDLE;
        }
    for (auto *p : {&lightingPixel_, &lightingCompute_, &skyPipeline_, &postPipeline_, &testPipeline_,
                    &tileDepthPipeline_, &cullPipeline_})
    {
        if (*p)
            vkDestroyPipeline(device_, *p, nullptr);
        *p = VK_NULL_HANDLE;
    }
    for (auto m : modules_)
        vkDestroyShaderModule(device_, m, nullptr);
    modules_.clear();
    for (auto *array : {&meshDirect_, &meshTask_})
        for (auto &p : *array)
        {
            if (p)
                vkDestroyPipeline(device_, p, nullptr);
            p = VK_NULL_HANDLE;
        }
    for (auto *p : {&meshReset_, &meshCull_, &meshFinalize_})
    {
        if (*p)
            vkDestroyPipeline(device_, *p, nullptr);
        *p = VK_NULL_HANDLE;
    }
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
void Sample::Bind(uint32_t object, uint32_t material, bool compute)
{
    uint32_t offset = uint32_t(object * constantStride_);
    vkCmdBindDescriptorSets(commands_,
                            compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_, 0, 1, &descriptorSets_[material], 1, &offset);
}

void Sample::Stamp(uint32_t i)
{
    if (queries_)
        vkCmdWriteTimestamp2(commands_, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, queries_, i);
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
    CheckVk(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "Frame completion");
    CreateLightGridResources();
    UpdateDescriptors();
    frameLights_ = MakeManyLights(workshop_.scenes[panel_.scene], panel_.grid);
    gridData_ = MakeLightGridConstants(workshop_, panel_.camera, panel_.pathEnabled ? panel_.pathFrame : -1,
                                       width_, height_, panel_.grid, panel_.navigation.Position(),
                                       panel_.navigation.Target(), panel_.navigation.Projection());
    std::memcpy(gridConstants_.mapped, &gridData_, sizeof(gridData_));
    Flush(gridConstants_);
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
    auto &s = workshop_.scenes[panel_.scene];
    auto fill = [&](const WorkshopInstance *instance)
    {
        auto c = MakeDeferredConstants(
            workshop_, panel_.scene, panel_.camera, panel_.pathEnabled ? panel_.pathFrame : -1, width_,
            height_, instance, uint32_t(std::min(panel_.view, 13)), panel_.direct, panel_.ibl, panel_.shadows,
            panel_.pcf, panel_.bias, panel_.exposure, panel_.intensity, panel_.yaw, panel_.occluder,
            panel_.navigation.Position(), panel_.navigation.Target(), panel_.navigation.Projection());
        c.controls[0] = uint32_t(frameLights_.size());
        return c;
    };
    auto global = fill(nullptr);
    meshCpuFrame_ = 0;
    UpdateMeshData(global);
    std::memcpy(constants_.mapped, &global, sizeof(global));
    for (uint32_t i = 0; i < s.instances.size(); ++i)
    {
        auto c = fill(&s.instances[i]);
        std::memcpy(static_cast<uint8_t *>(constants_.mapped) + (i + 1) * constantStride_, &c, sizeof(c));
    }
    Flush(constants_);
    if (!frameLights_.empty())
        std::memcpy(points_.mapped, frameLights_.data(), frameLights_.size() * sizeof(WorkshopLight));
    Flush(points_);
    for (uint32_t p = 0; p < 4; ++p)
    {
        PostConstants c{};
        c.settings = {panel_.exposure, panel_.threshold, panel_.strength, float(panel_.view >= 14 ? 2 : 0)};
        c.dimensions = {width_, height_, std::max(1u, (width_ + 1) / 2), std::max(1u, (height_ + 1) / 2)};
        c.flags = {uint32_t(panel_.bloom && !panel_.meshletView && panel_.view == 0 && panel_.grid.view == 0),
                   p == 1 ? 1u : 0u,
                   options_.testImage == L"black"      ? 1u
                   : options_.testImage == L"constant" ? 2u
                   : options_.testImage == L"impulse"  ? 3u
                                                       : 0u,
                   uint32_t(panel_.meshletView || panel_.grid.view != 0 ||
                            (panel_.view >= 1 && panel_.view <= 8 && panel_.view != 5))};
        std::memcpy(static_cast<uint8_t *>(postConstants_.mapped) + p * constantStride_, &c, sizeof(c));
    }
    Flush(postConstants_);
    uint32_t index = 0;
    auto acquire =
        vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable_, VK_NULL_HANDLE, &index);
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
    CheckVk(vkResetFences(device_, 1, &fence_), "Fence reset");
    CheckVk(vkResetCommandBuffer(commands_, 0), "Commands reset");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CheckVk(vkBeginCommandBuffer(commands_, &begin), "Frame begin");
    if (queries_)
        vkCmdResetQueryPool(commands_, queries_, 0, 26);
    Stamp(18);
    Stamp(24);
    if (MESH_TECHNIQUE == 5 || !options_.benchmark)
        ResetMeshStatistics();
    PrepareMeshIndirect();
    Stamp(25);
    Stamp(0);
    Move(shadow_.image, VK_IMAGE_ASPECT_DEPTH_BIT, shadowLayout_,
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    Begin({}, shadow_.view, true, false, 2048, 2048);
    if (panel_.shadows)
        DrawObjects(0);
    vkCmdEndRendering(commands_);
    Move(shadow_.image, VK_IMAGE_ASPECT_DEPTH_BIT, shadowLayout_,
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    Stamp(1);
    Stamp(2);
    Move(gbuffer_[4].image, VK_IMAGE_ASPECT_DEPTH_BIT, gLayouts_[4],
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    if (panel_.forward)
    {
        Move(hdr_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[0],
             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        Begin({hdr_[0].view}, gbuffer_[4].view, true, false, width_, height_);
        if (panel_.renderPath == 3)
            DrawMeshIndirect(2);
        else
            DrawObjects(2);
        vkCmdEndRendering(commands_);
    }
    else
    {
        std::vector<VkImageView> colors;
        for (int i = 0; i < 4; ++i)
        {
            Move(gbuffer_[i].image, VK_IMAGE_ASPECT_COLOR_BIT, gLayouts_[i],
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            colors.push_back(gbuffer_[i].view);
        }
        Begin(colors, gbuffer_[4].view, true, false, width_, height_);
        DrawObjects(1);
        vkCmdEndRendering(commands_);
        for (int i = 0; i < 4; ++i)
            Move(gbuffer_[i].image, VK_IMAGE_ASPECT_COLOR_BIT, gLayouts_[i],
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    Move(gbuffer_[4].image, VK_IMAGE_ASPECT_DEPTH_BIT, gLayouts_[4],
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    Stamp(3);
    BuildLightLists();
    Stamp(4);
    if (!panel_.forward)
    {
        if constexpr (DEFERRED_COMPUTE)
        {
            Move(hdr_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[0], VK_IMAGE_LAYOUT_GENERAL);
            vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, lightingCompute_);
            Bind(0, 0, true);
            vkCmdDispatch(commands_, (width_ + 7) / 8, (height_ + 7) / 8, 1);
        }
        else
        {
            Move(hdr_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[0],
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            Begin({hdr_[0].view}, VK_NULL_HANDLE, true, false, width_, height_);
            vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPixel_);
            Bind(0, 0);
            vkCmdDraw(commands_, 3, 1, 0, 0);
            vkCmdEndRendering(commands_);
        }
    }
    Stamp(5);
    Stamp(6);
    Move(hdr_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[0], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    Begin({hdr_[0].view}, gbuffer_[4].view, false, true, width_, height_);
    vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
    Bind(0, 0);
    if (MESH_TECHNIQUE != 4 && !panel_.meshletView && panel_.grid.view == 0)
        vkCmdDraw(commands_, 3, 1, 0, 0);
    vkCmdEndRendering(commands_);
    Stamp(7);
    Stamp(8);
    Move(hdr_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[0], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    Begin({hdr_[0].view}, gbuffer_[4].view, false, true, width_, height_);
    if (panel_.grid.view == 0)
        DrawObjects(3);
    vkCmdEndRendering(commands_);
    if (!options_.testImage.empty())
    {
        Move(hdr_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[0],
             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        Begin({hdr_[0].view}, VK_NULL_HANDLE, true, false, width_, height_);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, testPipeline_);
        vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout_, 0, 1, &postSets_[3],
                                0, nullptr);
        vkCmdDraw(commands_, 3, 1, 0, 0);
        vkCmdEndRendering(commands_);
    }
    Stamp(9);
    Move(hdr_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    for (uint32_t p = 0; p < 3; ++p)
    {
        Stamp(10 + p * 2);
        Move(hdr_[p + 1].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[p + 1], VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, bloomPipelines_[p == 0 ? 0 : 1]);
        vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, postLayout_, 0, 1, &postSets_[p],
                                0, nullptr);
        vkCmdDispatch(commands_, (std::max(1u, (width_ + 1) / 2) + 7) / 8,
                      (std::max(1u, (height_ + 1) / 2) + 7) / 8, 1);
        Move(hdr_[p + 1].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[p + 1],
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Stamp(11 + p * 2);
    }
    Stamp(16);
    ImageBarrier(commands_, images_[index], VK_IMAGE_ASPECT_COLOR_BIT,
                 presented_[index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    Begin({views_[index]}, VK_NULL_HANDLE, true, false, width_, height_);
    vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline_);
    vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout_, 0, 1, &postSets_[3], 0,
                            nullptr);
    vkCmdDraw(commands_, 3, 1, 0, 0);
    vkCmdEndRendering(commands_);
    ImageBarrier(commands_, images_[index], VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    Begin({views_[index]}, VK_NULL_HANDLE, false, false, width_, height_);
    RenderImGui();
    vkCmdEndRendering(commands_);
    Stamp(17);
    if (!options_.benchmark)
        CopyMeshStatistics();
    Stamp(19);
    CopyGridStatistics();
    if (capture_.buffer)
    {
        ImageBarrier(commands_, images_[index], VK_IMAGE_ASPECT_COLOR_BIT,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width_, height_, 1};
        vkCmdCopyImageToBuffer(commands_, images_[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               capture_.buffer, 1, &region);
        VkBufferMemoryBarrier2 host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host.buffer = capture_.buffer;
        host.size = VK_WHOLE_SIZE;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.bufferMemoryBarrierCount = 1;
        dependency.pBufferMemoryBarriers = &host;
        vkCmdPipelineBarrier2(commands_, &dependency);
    }
    ImageBarrier(
        commands_, images_[index], VK_IMAGE_ASPECT_COLOR_BIT,
        capture_.buffer ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        capture_.buffer ? VK_PIPELINE_STAGE_2_COPY_BIT : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        capture_.buffer ? VK_ACCESS_2_TRANSFER_READ_BIT : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE, 0);
    CheckVk(vkEndCommandBuffer(commands_), "vkEndCommandBuffer");
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
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    CheckVk(vkQueueSubmit2(queue_, 1, &submit, fence_), "vkQueueSubmit2");
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_[index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &index;
    const auto result = vkQueuePresentKHR(queue_, &present);
    if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
        CheckVk(result, "vkQueuePresentKHR");
    // Single frame in flight: keep synchronization and resource lifetimes visible.
    CheckVk(vkQueueWaitIdle(queue_), "vkQueueWaitIdle");
    presented_[index] = true;
    if (capture_.buffer)
    {
        if (!capture_.coherent)
        {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = capture_.memory;
            range.size = VK_WHOLE_SIZE;
            CheckVk(vkInvalidateMappedMemoryRanges(device_, 1, &range), "Readback invalidate");
        }
        auto *rgba = static_cast<uint8_t *>(capture_.mapped);
        if (colorFormat_ == VK_FORMAT_B8G8R8A8_SRGB)
            for (size_t pixel = 0; pixel < size_t(width_) * height_; ++pixel)
                std::swap(rgba[pixel * 4], rgba[pixel * 4 + 2]);
        SavePng(capturePath_, width_, height_, rgba, width_ * 4);
        DestroyBuffer(capture_);
        capturePath_.clear();
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquire == VK_SUBOPTIMAL_KHR)
        OnResize(width_, height_);

    if (!captureOnly_)
    {
        std::array<uint64_t, 26> ticks{};
        std::array<double, GpuPassCount> values{};
        if (queries_)
        {
            CheckVk(vkGetQueryPoolResults(device_, queries_, 0, 26, sizeof(ticks), ticks.data(), 8,
                                          VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                    "GPU timestamps");
            uint64_t mask = timestampBits_ == 64 ? UINT64_MAX : (uint64_t(1) << timestampBits_) - 1;
            for (int i = 0; i < GpuPassCount; ++i)
                values[i] = double((ticks[i * 2 + 1] - ticks[i * 2]) & mask) * timestampPeriod_ / 1e6;
        }
        timing_.Record(values);
        uint64_t meshMask = timestampBits_ == 64 ? UINT64_MAX : (uint64_t(1) << timestampBits_) - 1;
        timing_.RecordMesh(
            SKIN_TECHNIQUE == 2 ? double((ticks[25] - ticks[24]) & meshMask) * timestampPeriod_ * 1e-6 : 0,
            meshCpuFrame_);
        timing_.dualQuaternion = panel_.dualQuaternion;
    timing_.RecordSkin(skinning_.poseMilliseconds, skinning_.boundsMilliseconds, skinning_.conversionMilliseconds);
        ReadGridStatistics();
    }
}
void Sample::OnResize(uint32_t width, uint32_t height)
{
    if (!width || !height)
        return;
    CheckVk(vkDeviceWaitIdle(device_), "Resize completion");
    DestroyPipelines();
    DestroySwapchain();
    for (auto &g : gbuffer_)
        DestroyImage(g);
    for (auto &h : hdr_)
        DestroyImage(h);
    width_ = width;
    height_ = height;
    CreateSwapchain();
    CreateSizeResources();
    CreatePipeline();
    CreateMeshPipeline();
    if (uiPipeline_)
    {
        vkDestroyPipeline(device_, uiPipeline_, nullptr);
        uiPipeline_ = VK_NULL_HANDLE;
        vkDestroyShaderModule(device_, uiVertexShader_, nullptr);
        vkDestroyShaderModule(device_, uiPixelShader_, nullptr);
        uiVertexShader_ = uiPixelShader_ = VK_NULL_HANDLE;
        CreateUiPipeline();
    }
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
    const uint32_t sizes[] = {4, 4, 2, 4, 4, 8, 8, 8, 8, 4};
    for (uint32_t i = 0; i < 10; ++i)
    {
        if (panel_.forward && i < 4)
            continue;
        auto image = i == 9 ? shadow_.image : i < 5 ? gbuffer_[i].image : hdr_[i - 5].image;
        auto &layout = i == 9 ? shadowLayout_ : i < 5 ? gLayouts_[i] : hdrLayouts_[i - 5];
        auto before = layout;
        uint32_t w = i == 9   ? 2048
                     : i >= 6 ? std::max(1u, (width_ + 1) / 2)
                              : width_,
                 h = i == 9   ? 2048
                     : i >= 6 ? std::max(1u, (height_ + 1) / 2)
                              : height_;
        VkImageAspectFlags aspect =
            (i == 4 || i == 9) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        auto readback = CreateBuffer(size_t(w) * h * sizes[i], VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        CheckVk(vkResetCommandBuffer(commands_, 0), "Readback reset");
        VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVk(vkBeginCommandBuffer(commands_, &b), "Readback begin");
        Move(image, aspect, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {aspect, 0, 0, 1};
        copy.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(commands_, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1,
                               &copy);
        Move(image, aspect, layout, before);
        VkMemoryBarrier2 host{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.memoryBarrierCount = 1;
        d.pMemoryBarriers = &host;
        vkCmdPipelineBarrier2(commands_, &d);
        Submit();
        if (!readback.coherent)
        {
            VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            r.memory = readback.memory;
            r.size = VK_WHOLE_SIZE;
            CheckVk(vkInvalidateMappedMemoryRanges(device_, 1, &r), "Readback invalidate");
        }
        std::vector<uint8_t> bytes(size_t(w) * h * sizes[i]);
        std::memcpy(bytes.data(), readback.mapped, bytes.size());
        CaptureBytes(directory / (std::string(names[i]) + ".bin"), bytes.data(), bytes.size());
        if (i >= 5 && i < 9)
            metadata["images"].push_back(CaptureHalfImage(directory, names[i], w, h, bytes));
        DestroyBuffer(readback);
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
        if (device_)
            CheckVk(vkDeviceWaitIdle(device_), "Shutdown completion");
        timing_.Save();
    }
    catch (const std::exception &e)
    {
        ValidationError(e.what());
    }
    DestroyImGui();
    if (device_)
    {
        DestroyPipelines();
        DestroySwapchain();
        for (auto &g : geometry_)
        {
            DestroyBuffer(g.vertices);
            DestroyBuffer(g.indices);
        }
        geometry_.clear();
        for (auto &t : textures_)
            DestroyImage(t);
        textures_.clear();
        for (auto &b : skinBuffers_)
            DestroyBuffer(b);
        DestroyBuffer(skinCache_);
        for (auto &b : meshBuffers_)
            DestroyBuffer(b);
        DestroyBuffer(meshVisible_);
        DestroyBuffer(meshArgs_);
        DestroyBuffer(meshReadback_);
        for (auto &g : gbuffer_)
            DestroyImage(g);
        for (auto &h : hdr_)
            DestroyImage(h);
        for (auto &e : environment_)
            DestroyImage(e);
        DestroyImage(shadow_);
        DestroyImage(brdf_);
        for (auto &b : gridBuffers_)
            DestroyBuffer(b);
        DestroyBuffer(gridConstants_);
        DestroyBuffer(gridStatsReadback_);
        for (auto *b : {&constants_, &points_, &postConstants_, &staging_, &capture_})
            DestroyBuffer(*b);
        if (queries_)
            vkDestroyQueryPool(device_, queries_, nullptr);
        for (auto p : {descriptorPool_, postPool_})
            if (p)
                vkDestroyDescriptorPool(device_, p, nullptr);
        for (auto l : {pipelineLayout_, postLayout_})
            if (l)
                vkDestroyPipelineLayout(device_, l, nullptr);
        for (auto l : {descriptorLayout_, postDescriptorLayout_})
            if (l)
                vkDestroyDescriptorSetLayout(device_, l, nullptr);
        for (auto s : {sampler_, environmentSampler_})
            if (s)
                vkDestroySampler(device_, s, nullptr);
        if (imageAvailable_)
            vkDestroySemaphore(device_, imageAvailable_, nullptr);
        if (fence_)
            vkDestroyFence(device_, fence_, nullptr);
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
void Sample::OnScreenshot(const std::filesystem::path &path)
{
    // Read back an acquired image before presenting it; never access an image owned by presentation.
    capturePath_ = path;
    captureOnly_ = true;
    OnRender();
    captureOnly_ = false;
    if (!capturePath_.empty())
    {
        captureOnly_ = true;
        OnRender();
        captureOnly_ = false;
        if (!capturePath_.empty())
            throw std::runtime_error("Swapchain changed during screenshot capture");
    }
    if (panel_.captureGpu)
        DumpGpuData(path);
}

void Sample::CreateLightGridResources()
{
    uint32_t count = LightGridCellCount(width_, height_, panel_.grid.mode);
    if (count == gridCells_ && gridBuffers_[0].buffer)
        return;
    gridCells_ = count;
    uint32_t tiles = ((width_ + 15) / 16) * ((height_ + 15) / 16);
    const VkDeviceSize bytes[] = {count * 8ull, count * LightGridStride * 4ull, tiles * 8ull, tiles * 4ull};
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_, &properties);
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (bytes[i] > properties.limits.maxStorageBufferRange)
            throw std::runtime_error("Light-list buffer exceeds maxStorageBufferRange");
        DestroyBuffer(gridBuffers_[i]);
        gridBuffers_[i] = CreateBuffer(
            bytes[i], VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, nullptr, true);
    }
    DestroyBuffer(gridStatsReadback_);
    gridStatsReadback_ = CreateBuffer(count * 8ull, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
}
void Sample::BuildLightLists()
{
    auto barrier =
        [&](VkPipelineStageFlags2 src, VkAccessFlags2 access, VkPipelineStageFlags2 dst, VkAccessFlags2 next)
    {
        VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        b.srcStageMask = src;
        b.srcAccessMask = access;
        b.dstStageMask = dst;
        b.dstAccessMask = next;
        VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.memoryBarrierCount = 1;
        d.pMemoryBarriers = &b;
        vkCmdPipelineBarrier2(commands_, &d);
    };
    Stamp(20);
    if (!panel_.forward && panel_.grid.mode != 0)
    {
        barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, tileDepthPipeline_);
        Bind(0, 0, true);
        vkCmdDispatch(commands_, gridData_.size[0], gridData_.size[1], 1);
        barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    }
    Stamp(21);
    Stamp(22);
    if (!panel_.forward && panel_.grid.mode != 0)
    {
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipeline_);
        Bind(0, 0, true);
        vkCmdDispatch(commands_, gridData_.size[0], gridData_.size[1],
                      panel_.grid.mode == 2 ? LightGridSlices : 1);
        barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    }
    Stamp(23);
}
void Sample::CopyGridStatistics()
{
    if (panel_.forward || panel_.grid.mode == 0)
        return;
    VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    b.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer = gridBuffers_[0].buffer;
    b.size = VK_WHOLE_SIZE;
    VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    d.bufferMemoryBarrierCount = 1;
    d.pBufferMemoryBarriers = &b;
    vkCmdPipelineBarrier2(commands_, &d);
    VkBufferCopy copy{0, 0, gridCells_ * 8ull};
    vkCmdCopyBuffer(commands_, gridBuffers_[0].buffer, gridStatsReadback_.buffer, 1, &copy);
    b.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    b.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    b.buffer = gridStatsReadback_.buffer;
    vkCmdPipelineBarrier2(commands_, &d);
}
void Sample::ReadGridStatistics()
{
    panel_.gridStatistics = {};
    if (!panel_.forward && panel_.grid.mode != 0)
    {
        if (!gridStatsReadback_.coherent)
        {
            VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            r.memory = gridStatsReadback_.memory;
            r.size = VK_WHOLE_SIZE;
            CheckVk(vkInvalidateMappedMemoryRanges(device_, 1, &r), "Grid statistics invalidate");
        }
        panel_.gridStatistics.Read(static_cast<const uint32_t *>(gridStatsReadback_.mapped), gridCells_);
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
    uint32_t tiles = gridData_.size[0] * gridData_.size[1];
    const VkDeviceSize sizes[] = {gridCells_ * 8ull, gridCells_ * LightGridStride * 4ull, tiles * 8ull,
                                  tiles * 4ull};
    const char *names[] = {"light-headers.bin", "light-indices.bin", "tile-depth-bounds.bin",
                           "tile-slice-masks.bin"};
    for (uint32_t i = 0; i < 4; ++i)
    {
        auto readback = CreateBuffer(sizes[i], VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        CheckVk(vkResetCommandBuffer(commands_, 0), "List capture reset");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVk(vkBeginCommandBuffer(commands_, &begin), "List capture begin");
        VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        b.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.memoryBarrierCount = 1;
        d.pMemoryBarriers = &b;
        vkCmdPipelineBarrier2(commands_, &d);
        VkBufferCopy copy{0, 0, sizes[i]};
        vkCmdCopyBuffer(commands_, gridBuffers_[i].buffer, readback.buffer, 1, &copy);
        b.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        b.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        vkCmdPipelineBarrier2(commands_, &d);
        CheckVk(vkEndCommandBuffer(commands_), "List capture end");
        VkCommandBufferSubmitInfo cmd{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmd.commandBuffer = commands_;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmd;
        CheckVk(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE), "List capture submit");
        CheckVk(vkQueueWaitIdle(queue_), "List capture completion");
        if (!readback.coherent)
        {
            VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            r.memory = readback.memory;
            r.size = VK_WHOLE_SIZE;
            CheckVk(vkInvalidateMappedMemoryRanges(device_, 1, &r), "List capture invalidate");
        }
        std::ofstream(directory / names[i], std::ios::binary)
            .write(static_cast<const char *>(readback.mapped), sizes[i]);
        DestroyBuffer(readback);
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
    for (uint32_t i = 0; i < 10; ++i)
        meshBuffers_[i] = CreateBuffer(std::max(size_t(1), counts[i]) * strides[i],
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                           (i == 1 ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : 0),
                                       counts[i] ? data[i] : nullptr);
    meshVisible_ =
        CreateBuffer(uint64_t(candidateCapacity_) * 16,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, nullptr, true);
    meshArgs_ = CreateBuffer(256,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             nullptr, true);
    meshReadback_ = CreateBuffer(256, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
}
void Sample::UpdateMeshDescriptors()
{
    for (auto set : descriptorSets_)
    {
        std::array<VkDescriptorBufferInfo, 17> buffers{};
        std::array<VkDescriptorImageInfo, 64> images{};
        std::vector<VkWriteDescriptorSet> writes;
        for (uint32_t i = 0; i < 17; ++i)
        {
            auto &b = i < 10    ? meshBuffers_[i]
                      : i == 10 ? meshVisible_
                      : i < 13  ? meshArgs_
                      : i < 16  ? skinBuffers_[i - 13]
                                : skinCache_;
            buffers[i] = {b.buffer, 0, VK_WHOLE_SIZE};
            VkWriteDescriptorSet d{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            d.dstSet = set;
            d.dstBinding = i == 12 ? 37 : i >= 13 ? 38 + i - 13 : 24 + i;
            d.descriptorCount = 1;
            d.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            d.pBufferInfo = &buffers[i];
            writes.push_back(d);
        }
        for (uint32_t i = 0; i < 64; ++i)
        {
            images[i].imageView = textures_[i < textures_.size() ? i : 0].view;
            images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        VkWriteDescriptorSet d{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        d.dstSet = set;
        d.dstBinding = 36;
        d.descriptorCount = 64;
        d.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        d.pImageInfo = images.data();
        writes.push_back(d);
        vkUpdateDescriptorSets(device_, uint32_t(writes.size()), writes.data(), 0, nullptr);
    }
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
    auto copy = [&](uint32_t i, const void *data, size_t size)
    {
        if (size)
            std::memcpy(meshBuffers_[i].mapped, data, size);
        Flush(meshBuffers_[i]);
    };
    copy(0, meshFrames_.data(), sizeof(meshFrames_));
    copy(8, meshInstances_.data(), meshInstances_.size() * sizeof(MeshInstance));
    copy(9, meshCandidates_.data(), meshCandidates_.size() * 16);
    if (meshStatisticsReady_)
    {
        if (!meshReadback_.coherent)
        {
            VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            r.memory = meshReadback_.memory;
            r.size = VK_WHOLE_SIZE;
            CheckVk(vkInvalidateMappedMemoryRanges(device_, 1, &r), "Mesh statistics invalidate");
        }
        auto u = static_cast<const uint32_t *>(meshReadback_.mapped);
        panel_.candidateCount = uint32_t(meshCandidates_.size());
        panel_.visibleCount = panel_.renderPath == 3   ? u[0] + u[8]
                              : panel_.renderPath == 2 ? u[37]
                                                       : uint32_t(meshCandidates_.size());
    }
}
void Sample::BindMesh(int pass, uint32_t instance, uint32_t base, uint32_t mode, bool compute)
{
    Bind(0, 0, compute);
    mode |= panel_.dualQuaternion ? 0x10000u : 0u;
    uint32_t push[] = {instance, base, uint32_t(pass), mode};
    vkCmdPushConstants(commands_, pipelineLayout_, meshStageFlags_, 0, 16, push);
}
void Sample::MeshBarrier(VkPipelineStageFlags2 source, VkAccessFlags2 sourceAccess,
                         VkPipelineStageFlags2 destination, VkAccessFlags2 destinationAccess)
{
    VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask = source;
    b.srcAccessMask = sourceAccess;
    b.dstStageMask = destination;
    b.dstAccessMask = destinationAccess;
    VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    d.memoryBarrierCount = 1;
    d.pMemoryBarriers = &b;
    vkCmdPipelineBarrier2(commands_, &d);
}
void Sample::ResetMeshStatistics()
{
    MeshBarrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    BindMesh(1, 0, meshDispatchWidth_, 3, true);
    vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, meshReset_);
    vkCmdDispatch(commands_, 1, 1, 1);
    MeshBarrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                    (MESH_TECHNIQUE >= 6 ? VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT : 0) |
                    (SKIN_TECHNIQUE == 3 ? VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT : 0),
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}
void Sample::DrawObjects(int pass)
{
    MeshCpuTimer cpu{meshCpuFrame_};
    if (pass == 3)
        return;
    const auto &scene = workshop_.scenes[panel_.scene];
    if (panel_.renderPath == 3)
    {
        throw std::runtime_error("Indirect mesh preparation must run before BeginRendering");
    }
    for (uint32_t i = 0; i < scene.instances.size(); ++i)
    {
        const auto &object = scene.instances[i];
        const auto &primitive = meshData_.primitives[object.primitive];
        const auto &material = workshop_.materials[primitive.offsets.z];
        if (!panel_.Includes(material.alpha))
            continue;
        uint32_t bin = material.doubleSided ? 1u : 0u;
        if (panel_.renderPath == 0)
        {
            BindMesh(pass, i, 0, 0, false);
            vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              (pass == 0   ? shadowPipelines_
                               : pass == 1 ? gPipelines_
                                           : forwardPipelines_)[bin]);
            if constexpr (SKIN_TECHNIQUE == 2)
            {
                bool floor = meshInstances_[i].primitive.y == UINT32_MAX;
                VkDeviceSize offset =
                    ((floor ? 0 : VkDeviceSize(meshInstances_[i].primitive.y)) + primitive.offsets.x) * 48;
                VkBuffer buffer = floor ? meshBuffers_[1].buffer : skinCache_.buffer;
                vkCmdBindVertexBuffers(commands_, 0, 1, &buffer, &offset);
            }
            vkCmdBindIndexBuffer(commands_, geometry_[object.primitive].indices.buffer, 0,
                                 VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commands_, primitive.offsets.w, 1, 0, 0, 0);
        }
        else
        {
            bool task = panel_.renderPath == 2;
            uint32_t count = task ? (primitive.meshlets.y + 31) / 32 : primitive.meshlets.y;
            BindMesh(pass, i, task ? 0 : primitive.meshlets.x, task ? 2 : 1, false);
            vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              (task ? meshTask_ : meshDirect_)[pass * 2 + bin]);
            drawMesh_(commands_, count, 1, 1);
        }
    }
}
void Sample::PrepareMeshIndirect()
{

    if (options_.captureGpu)
    {
        MeshBarrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        uint32_t actors = panel_.scene ? 64 : 1;
        for (uint32_t actor = 0; actor < actors; ++actor)
        {
            BindMesh(1, actor * uint32_t(skinning_.model.primitives.size()), skinning_.vertexCount, 0, true);
            vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, skinCompute_);
            vkCmdDispatch(commands_, (skinning_.vertexCount + 63) / 64, 1, 1);
        }
        MeshBarrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
    }
}
void Sample::DrawMeshIndirect(int pass)
{
    MeshCpuTimer cpu{meshCpuFrame_};
    for (uint32_t bin = 0; bin < 2; ++bin)
    {
        BindMesh(pass, 0, meshDispatchWidth_, 3 | (bin << 8), false);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, meshDirect_[pass * 2 + bin]);
        drawMeshIndirect_(commands_, meshArgs_.buffer, bin * 32 + 16, 1, 12);
    }
}
void Sample::CreateMeshPipeline()
{
    auto shader = [&](const wchar_t *name)
    {
        auto bytes =
            ReadBinary(AssetPath(std::filesystem::path(L"Shaders") / (std::wstring(name) + L".spv")));
        VkShaderModuleCreateInfo d{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        d.codeSize = bytes.size();
        d.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
        VkShaderModule module;
        CheckVk(vkCreateShaderModule(device_, &d, nullptr, &module), "Mesh shader module");
        modules_.push_back(module);
        return module;
    };
    auto compute = [&](VkPipeline &out, const wchar_t *name)
    {
        VkComputePipelineCreateInfo d{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        d.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        d.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        d.stage.module = shader(name);
        d.stage.pName = "CSMain";
        d.layout = pipelineLayout_;
        CheckVk(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &d, nullptr, &out),
                "Mesh compute pipeline");
    };
    compute(meshReset_, L"ResetMeshlets");
    if (MESH_TECHNIQUE == 5)
    {
        compute(meshCull_, L"CullMeshlets");
        compute(meshFinalize_, L"FinalizeMeshlets");
    }
    if (options_.captureGpu)
        compute(skinCompute_, L"SkinningCompute");

#if SKIN_TECHNIQUE == 3
    for (uint32_t task = 0; task < 2; ++task)
    {
        if (task && MESH_TECHNIQUE < 6)
            continue;
        auto mesh = shader(task ? L"TaskMesh" : L"MeshletMesh");
        VkShaderModule amplification = task ? shader(L"MeshletTask") : VK_NULL_HANDLE;
        for (uint32_t pass = 0; pass < 3; ++pass)
            for (uint32_t bin = 0; bin < 2; ++bin)
            {
                std::array<VkPipelineShaderStageCreateInfo, 3> stages{};
                for (auto &s : stages)
                    s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[0].stage = VK_SHADER_STAGE_MESH_BIT_EXT;
                stages[0].module = mesh;
                stages[0].pName = "MSMain";
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = shader(pass == 0   ? L"MeshShadowPixel"
                                          : pass == 1 ? L"MeshGBufferPixel"
                                                      : L"MeshForwardPixel");
                stages[1].pName = "PSMain";
                if (task)
                {
                    stages[2].stage = VK_SHADER_STAGE_TASK_BIT_EXT;
                    stages[2].module = amplification;
                    stages[2].pName = "ASMain";
                }
                VkPipelineViewportStateCreateInfo viewport{
                    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
                viewport.viewportCount = viewport.scissorCount = 1;
                VkPipelineRasterizationStateCreateInfo raster{
                    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
                raster.polygonMode = VK_POLYGON_MODE_FILL;
                raster.cullMode = bin ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
                raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
                raster.lineWidth = 1;
                VkPipelineMultisampleStateCreateInfo sample{
                    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
                sample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
                VkPipelineDepthStencilStateCreateInfo depth{
                    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
                depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
                depth.depthCompareOp = VK_COMPARE_OP_LESS;
                std::array<VkPipelineColorBlendAttachmentState, 4> attachments{};
                for (auto &a : attachments)
                    a.colorWriteMask = 15;
                VkPipelineColorBlendStateCreateInfo blend{
                    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
                blend.attachmentCount = pass == 0 ? 0 : pass == 1 ? 4 : 1;
                blend.pAttachments = attachments.data();
                const VkFormat formats[] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R16G16_SNORM,
                                            VK_FORMAT_R8G8_UNORM, VK_FORMAT_B10G11R11_UFLOAT_PACK32};
                VkFormat color = VK_FORMAT_R16G16B16A16_SFLOAT;
                VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                rendering.colorAttachmentCount = blend.attachmentCount;
                rendering.pColorAttachmentFormats = pass == 1 ? formats : &color;
                rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
                const VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
                VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
                dy.dynamicStateCount = 2;
                dy.pDynamicStates = dynamic;
                VkGraphicsPipelineCreateInfo d{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                d.pNext = &rendering;
                d.stageCount = task ? 3 : 2;
                d.pStages = stages.data();
                d.pViewportState = &viewport;
                d.pRasterizationState = &raster;
                d.pMultisampleState = &sample;
                d.pDepthStencilState = &depth;
                d.pColorBlendState = &blend;
                d.pDynamicState = &dy;
                d.layout = pipelineLayout_;
                auto &out = (task ? meshTask_ : meshDirect_)[pass * 2 + bin];
                CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &d, nullptr, &out),
                        "EXT mesh pipeline");
            }
    }

#endif
}
void Sample::CopyMeshStatistics()
{
    MeshBarrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferCopy copy{0, 0, 256};
    vkCmdCopyBuffer(commands_, meshArgs_.buffer, meshReadback_.buffer, 1, &copy);
    MeshBarrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                VK_ACCESS_2_HOST_READ_BIT);
    meshStatisticsReady_ = true;
}
void Sample::DumpMeshData(const std::filesystem::path &directory, nlohmann::json &metadata)
{
    CaptureBytes(directory / "mesh-vertices.bin", meshData_.vertices.data(),
                 meshData_.vertices.size() * sizeof(MeshVertex));
    CaptureBytes(directory / "mesh-original-indices.bin", meshData_.indices.data(),
                 meshData_.indices.size() * 4);
    CaptureBytes(directory / "mesh-global.bin", constants_.mapped, sizeof(DeferredConstants));
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
    auto read = [&](const Buffer &source, VkDeviceSize bytes, const char *name)
    {
        CheckVk(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "Mesh diagnostic completion");
        auto result = CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        CheckVk(vkResetCommandBuffer(commands_, 0), "Mesh diagnostic reset");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVk(vkBeginCommandBuffer(commands_, &begin), "Mesh diagnostic begin");
        MeshBarrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferCopy copy{0, 0, bytes};
        vkCmdCopyBuffer(commands_, source.buffer, result.buffer, 1, &copy);
        MeshBarrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        Submit();
        if (!result.coherent)
        {
            VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            r.memory = result.memory;
            r.size = VK_WHOLE_SIZE;
            CheckVk(vkInvalidateMappedMemoryRanges(device_, 1, &r), "Mesh diagnostic invalidate");
        }
        CaptureBytes(directory / name, result.mapped, size_t(bytes));
        DestroyBuffer(result);
    };
    read(meshArgs_, 256, "mesh-arguments.bin");
    read(meshVisible_, uint64_t(candidateCapacity_) * 16, "mesh-visible.bin");
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
    skinBuffers_[0] = CreateBuffer(skinning_.influences.size() * sizeof(SkinInfluence),
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, skinning_.influences.data());
    skinBuffers_[1] = CreateBuffer(skinning_.palettes.size() * 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   skinning_.palettes.data());
    skinBuffers_[2] = CreateBuffer(skinning_.dualQuaternions.size() * 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   skinning_.dualQuaternions.data());
    skinCache_ = CreateBuffer((options_.captureGpu ? uint64_t(64) * skinning_.vertexCount : 1) * 48,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              nullptr, true);
}
void Sample::UpdateSkinBuffers()
{
    std::memcpy(skinBuffers_[1].mapped, skinning_.palettes.data(), skinning_.palettes.size() * 64);
    Flush(skinBuffers_[1]);
    std::memcpy(skinBuffers_[2].mapped, skinning_.dualQuaternions.data(), skinning_.dualQuaternions.size() * 32);
    Flush(skinBuffers_[2]);
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
            reference.push_back(panel_.dualQuaternion ? DeformSkinVertexDqs(source,
                std::span<const SkinDualQuaternion>(skinning_.dualQuaternions).subspan(actor * skinning_.jointCount, skinning_.jointCount))
                : DeformSkinVertex(source, palette));
        }
    }
    CaptureBytes(directory / "skin-reference.bin", reference.data(), reference.size() * 48);
    CaptureBytes(directory / "skin-palettes.bin", skinning_.palettes.data(),
                 actors * skinning_.jointCount * 64);
    CaptureBytes(directory / "skin-influences.bin", skinning_.influences.data(),
                 skinning_.influences.size() * 32);
    CaptureBytes(directory / "skin-dual-quaternions.bin", skinning_.dualQuaternions.data(),
                 actors * skinning_.jointCount * 32);

    if (options_.captureGpu)
    {
        VkDeviceSize bytes = VkDeviceSize(actors) * skinning_.vertexCount * 48;
        auto readback = CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        CheckVk(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "Skin readback wait");
        CheckVk(vkResetCommandBuffer(commands_, 0), "Skin readback reset");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVk(vkBeginCommandBuffer(commands_, &begin), "Skin readback begin");
        MeshBarrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferCopy copy{0, 0, bytes};
        vkCmdCopyBuffer(commands_, skinCache_.buffer, readback.buffer, 1, &copy);
        MeshBarrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        Submit();
        if (!readback.coherent)
        {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = readback.memory;
            range.size = VK_WHOLE_SIZE;
            CheckVk(vkInvalidateMappedMemoryRanges(device_, 1, &range), "Skin readback invalidate");
        }
        auto values = static_cast<const SkinDeformedVertex *>(readback.mapped);
        for (uint32_t actor = 0; actor < actors; ++actor)
            for (uint32_t i = 0; i < skinning_.vertexCount; i += step)
                actual.push_back(values[actor * skinning_.vertexCount + i]);
        CaptureBytes(directory / "skin-cache.bin", actual.data(), actual.size() * 48);
        DestroyBuffer(readback);
    }

    nlohmann::json metadata = {
        {"technique", SKIN_TECHNIQUE},
        {"skinning", panel_.dualQuaternion ? "dqs" : "lbs"},
        {"dqConversionCpuMs", skinning_.conversionMilliseconds},
        {"dqPaletteBytes", actors * skinning_.jointCount * 32},
        {"diagnosticComputeReadback", options_.captureGpu},
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
