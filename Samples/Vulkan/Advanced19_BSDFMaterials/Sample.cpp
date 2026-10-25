#include "Sample.h"
#include "../../Shared/PostProcessCapture.h"
#include "../../Shared/RaytracingFixtures.h"
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
    workshop_ = LoadBSDFScene(options.scene.empty() ? AssetPath(L"Assets/Scenes/BSDFMaterials/BSDFMaterials.json") : options.scene);
    workshop_ = BSDFFixture(std::move(workshop_), options.bsdfTest);
    panel_.Initialize(workshop_, options);
    window_ = window;
    width_ = width;
    height_ = height;
    havePrevious_ = false;
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
        VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR as{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
        supported.pNext = &features;
        features.pNext = &f12;
        f12.pNext = &as;
        as.pNext = &rt;
        vkGetPhysicalDeviceFeatures2(candidate, &supported);
        if (!f12.bufferDeviceAddress || !f12.shaderSampledImageArrayNonUniformIndexing ||
            !supported.features.shaderSampledImageArrayDynamicIndexing || !as.accelerationStructure ||
            !rt.rayTracingPipeline)
            continue;
        if (!features.dynamicRendering || !features.synchronization2 ||
            !features.shaderDemoteToHelperInvocation ||
            (true && !supported.features.shaderStorageImageExtendedFormats))
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
        bool complete = true;
        for (const char *name :
             {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
              VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME})
            if (std::none_of(deviceExtensions.begin(), deviceExtensions.end(),
                             [&](auto &e) { return std::strcmp(e.extensionName, name) == 0; }))
                complete = false;
        if (!complete)
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
        throw std::runtime_error("Vulkan 1.3, KHR accelerationStructure / rayTracingPipeline / "
                                 "deferredHostOperations, bufferDeviceAddress, "
                                 "non-uniform sampled images, and graphics+compute+present are required");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_, &properties);
    Trace(std::string("GPU: ") + properties.deviceName);
    const float priority = 1;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    const char *deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
    VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features.dynamicRendering = VK_TRUE;
    features.synchronization2 = VK_TRUE;
    features.shaderDemoteToHelperInvocation = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.bufferDeviceAddress = f12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    as.accelerationStructure = VK_TRUE;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    rt.rayTracingPipeline = VK_TRUE;
    features.pNext = &f12;
    f12.pNext = &as;
    as.pNext = &rt;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkPhysicalDeviceFeatures basic{};
    basic.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    basic.shaderStorageImageExtendedFormats = true ? VK_TRUE : VK_FALSE;
    deviceInfo.pEnabledFeatures = &basic;
    deviceInfo.pNext = &features;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 4;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;
    CheckVk(vkCreateDevice(physical_, &deviceInfo, nullptr, &device_), "vkCreateDevice");
#define LOAD_RT(field, name)                                                                                 \
    field = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device_, #name));                               \
    if (!field)                                                                                              \
    throw std::runtime_error("Missing " #name)
    LOAD_RT(createAS_, vkCreateAccelerationStructureKHR);
    LOAD_RT(destroyAS_, vkDestroyAccelerationStructureKHR);
    LOAD_RT(getBuildSizes_, vkGetAccelerationStructureBuildSizesKHR);
    LOAD_RT(buildAS_, vkCmdBuildAccelerationStructuresKHR);
    LOAD_RT(getASAddress_, vkGetAccelerationStructureDeviceAddressKHR);
    LOAD_RT(createRTP_, vkCreateRayTracingPipelinesKHR);
    LOAD_RT(getShaderHandles_, vkGetRayTracingShaderGroupHandlesKHR);
    LOAD_RT(traceRays_, vkCmdTraceRaysKHR);
#undef LOAD_RT
    VkPhysicalDeviceProperties2 rtProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    rtProps.pNext = &rtProperties_;
    rtProperties_.pNext = &asProperties_;
    vkGetPhysicalDeviceProperties2(physical_, &rtProps);
    if (rtProperties_.maxRayRecursionDepth < 1 ||
        rtProps.properties.limits.maxPerStageDescriptorSampledImages < 68 ||
        asProperties_.maxDescriptorSetAccelerationStructures < 1)
        throw std::runtime_error("Insufficient raytracing recursion / descriptor limits");
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
    CreateSwapchain();
    CreateScene();
    CreateSizeResources();
    CreatePipeline();

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
    return cameraHandled || result != 0 || (io.WantCaptureKeyboard &&
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
    b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                     VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
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
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR);
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VkPipelineStageFlags2(VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                         VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR);
        case VK_IMAGE_LAYOUT_GENERAL:
            return VkPipelineStageFlags2(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                         VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR);
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
    rayData_ = MakeBSDFSceneData(workshop_);
    auto input = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    rayVertices_ =
        CreateBuffer(rayData_.vertices.size() * sizeof(RayVertex), input, rayData_.vertices.data());
    rayIndices_ = CreateBuffer(rayData_.indices.size() * 4, input, rayData_.indices.data());
    rayPrimitives_ = CreateBuffer(rayData_.primitives.size() * sizeof(RayPrimitive),
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, rayData_.primitives.data());
    rayMaterials_ = CreateBuffer(rayData_.materials.size() * sizeof(BSDFMaterial),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, rayData_.materials.data());
    for (const auto &im : workshop_.images)
    {
        auto mips = GenerateMipChain(RayTextureImage(im), im.semantic, im.alphaCutoff);
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
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                               VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
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

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_, &props);
    constantStride_ = std::max(VkDeviceSize(256), props.limits.minUniformBufferOffsetAlignment);
    constants_ = CreateBuffer(constantStride_, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    postConstants_ = CreateBuffer(4 * constantStride_, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    points_ = CreateBuffer(72 * sizeof(WorkshopLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    CheckVk(vkCreateSampler(device_, &sampler, nullptr, &sampler_), "Material sampler");
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    CheckVk(vkCreateSampler(device_, &sampler, nullptr, &environmentSampler_), "Environment sampler");
    const auto stages = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                        VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (uint32_t b = 0; b < 19; ++b)
    {
        auto type = b == 0              ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                    : b == 2 || b == 14 ? VK_DESCRIPTOR_TYPE_SAMPLER
                    : b == 3            ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
                    : (b >= 4 && b <= 9) || b == 18 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                    : b >= 15           ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                        : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings.push_back({b, type, b == 1 ? 64u : 1u, VkShaderStageFlags(stages), nullptr});
    }
    VkDescriptorSetLayoutCreateInfo l{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    l.bindingCount = uint32_t(bindings.size());
    l.pBindings = bindings.data();
    CheckVk(vkCreateDescriptorSetLayout(device_, &l, nullptr, &descriptorLayout_), "Ray descriptor layout");
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 68},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 2},        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},  {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1}};
    VkDescriptorPoolCreateInfo p{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    p.maxSets = 1;
    p.poolSizeCount = 6;
    p.pPoolSizes = sizes;
    CheckVk(vkCreateDescriptorPool(device_, &p, nullptr, &descriptorPool_), "Ray descriptor pool");
    VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    a.descriptorPool = descriptorPool_;
    a.descriptorSetCount = 1;
    a.pSetLayouts = &descriptorLayout_;
    descriptorSets_.resize(1);
    CheckVk(vkAllocateDescriptorSets(device_, &a, descriptorSets_.data()), "Ray descriptor set");
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
    pi.pSetLayouts = &descriptorLayout_;
    CheckVk(vkCreatePipelineLayout(device_, &pi, nullptr, &pipelineLayout_), "Scene pipeline layout");
    pi.pSetLayouts = &postDescriptorLayout_;
    CheckVk(vkCreatePipelineLayout(device_, &pi, nullptr, &postLayout_), "Post pipeline layout");
    BuildBlas();
    BuildTlas();
}

VkDeviceAddress Sample::Address(const Buffer &b)
{
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    info.buffer = b.buffer;
    return vkGetBufferDeviceAddress(device_, &info);
}
Sample::Buffer Sample::DeviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
    Buffer b;
    try
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        CheckVk(vkCreateBuffer(device_, &info, nullptr, &b.buffer), "AS buffer");
        VkMemoryRequirements r{};
        vkGetBufferMemoryRequirements(device_, b.buffer, &r);
        VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
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
void Sample::BuildBlas()
{
    blas_.resize(rayData_.primitives.size());
    for (size_t i = 0; i < blas_.size(); ++i)
    {
        const auto &p = rayData_.primitives[i].offsets;
        if (workshop_.materials[p.z].alpha == AlphaMode::Blend)
            continue;
        VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = workshop_.materials[p.z].alpha == AlphaMode::Opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
        auto &t = geometry.geometry.triangles;
        t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        t.vertexData.deviceAddress = Address(rayVertices_) + uint64_t(p.x) * sizeof(RayVertex);
        t.vertexStride = sizeof(RayVertex);
        t.maxVertex = uint32_t(workshop_.primitives[i].vertices.size() - 1);
        t.indexType = VK_INDEX_TYPE_UINT32;
        t.indexData.deviceAddress = Address(rayIndices_) + uint64_t(p.y) * 4;
        VkAccelerationStructureBuildGeometryInfoKHR build{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build.geometryCount = 1;
        build.pGeometries = &geometry;
        uint32_t count = p.w / 3;
        VkAccelerationStructureBuildSizesInfoKHR sizes{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        getBuildSizes_(device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &count, &sizes);
        auto &as = blas_[i];
        as.storage = DeviceBuffer(sizes.accelerationStructureSize,
                                  VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);
        VkAccelerationStructureCreateInfoKHR create{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        create.type = build.type;
        create.buffer = as.storage.buffer;
        create.size = sizes.accelerationStructureSize;
        CheckVk(createAS_(device_, &create, nullptr, &as.object), "Create BLAS");
        uint64_t alignment = asProperties_.minAccelerationStructureScratchOffsetAlignment;
        auto scratch = DeviceBuffer(sizes.buildScratchSize + alignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        build.scratchData.deviceAddress = (Address(scratch) + alignment - 1) / alignment * alignment;
        build.dstAccelerationStructure = as.object;
        CheckVk(vkResetCommandBuffer(commands_, 0), "BLAS reset");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVk(vkBeginCommandBuffer(commands_, &begin), "BLAS begin");
        VkAccelerationStructureBuildRangeInfoKHR range{count, 0, 0, 0};
        const auto *ranges = &range;
        buildAS_(commands_, 1, &build, &ranges);
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                               VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commands_, &dep);
        Submit();
        DestroyBuffer(scratch);
    }
    Trace("Shared BLAS: " + std::to_string(std::count_if(blas_.begin(), blas_.end(), [](auto &b)
                                                         { return b.object != VK_NULL_HANDLE; })));
}
void Sample::BuildTlas()
{
    CheckVk(vkDeviceWaitIdle(device_), "TLAS completion");
    DestroyAcceleration(tlas_);
    DestroyBuffer(rayInstancesBuffer_);
    rayInstances_ = MakeRayInstances(workshop_, panel_.scene, panel_.preset, panel_.occluder);
    havePrevious_ = false;
    pathLights_ = MakePathLights(workshop_, rayInstances_);
    DestroyBuffer(areaLights_);
    areaLights_ = CreateBuffer(pathLights_.lights.size() * sizeof(PathLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pathLights_.lights.data());

    rayInstancesBuffer_ = CreateBuffer(rayInstances_.size() * sizeof(RayInstance),
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, rayInstances_.data());
    std::vector<VkAccelerationStructureInstanceKHR> instances(rayInstances_.size());
    for (size_t i = 0; i < instances.size(); ++i)
    {
        auto &d = instances[i];
        const auto &r = rayInstances_[i];
        const auto *m = &r.world.m[0][0];
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 4; ++col)
                d.transform.matrix[row][col] = m[col * 4 + row];
        d.instanceCustomIndex = uint32_t(i);
        d.mask = 255;
        const auto &material = workshop_.materials[workshop_.primitives[r.primitive.x].material];
        d.flags = (material.doubleSided || material.transmission > 0) ? VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR : 0;
        VkAccelerationStructureDeviceAddressInfoKHR info{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        info.accelerationStructure = blas_[r.primitive.x].object;
        d.accelerationStructureReference = getASAddress_(device_, &info);
    }
    auto upload = CreateBuffer(instances.size() * sizeof(instances[0]),
                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                               instances.data());
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = Address(upload);
    VkAccelerationStructureBuildGeometryInfoKHR build{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    uint32_t count = uint32_t(instances.size());
    VkAccelerationStructureBuildSizesInfoKHR sizes{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    getBuildSizes_(device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &count, &sizes);
    tlas_.storage =
        DeviceBuffer(sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);
    VkAccelerationStructureCreateInfoKHR create{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    create.type = build.type;
    create.buffer = tlas_.storage.buffer;
    create.size = sizes.accelerationStructureSize;
    CheckVk(createAS_(device_, &create, nullptr, &tlas_.object), "Create TLAS");
    uint64_t alignment = asProperties_.minAccelerationStructureScratchOffsetAlignment;
    auto scratch = DeviceBuffer(sizes.buildScratchSize + alignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    build.scratchData.deviceAddress = (Address(scratch) + alignment - 1) / alignment * alignment;
    build.dstAccelerationStructure = tlas_.object;
    CheckVk(vkResetCommandBuffer(commands_, 0), "TLAS reset");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CheckVk(vkBeginCommandBuffer(commands_, &begin), "TLAS begin");
    VkAccelerationStructureBuildRangeInfoKHR range{count, 0, 0, 0};
    const auto *ranges = &range;
    buildAS_(commands_, 1, &build, &ranges);
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands_, &dep);
    Submit();
    DestroyBuffer(scratch);
    DestroyBuffer(upload);
    builtScene_ = panel_.scene;
    builtPreset_ = panel_.preset;
    builtOccluder_ = panel_.occluder;
    Trace("TLAS instances: " + std::to_string(instances.size()));
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
    for (uint32_t i = 0; i < 4; ++i)
    {
        hdr_[i] = CreateImage(i ? std::max(1u, (width_ + 1) / 2) : width_,
                              i ? std::max(1u, (height_ + 1) / 2) : height_, (i ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT),
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT);
        hdrLayouts_[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    for (uint32_t i = 0; i < 2; ++i)
    {
        diagnostic_[i] = CreateImage(width_, height_, VK_FORMAT_R32G32B32A32_SFLOAT,
                                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                     VK_IMAGE_ASPECT_COLOR_BIT);
        diagnosticLayouts_[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    UpdateDescriptors();
}
void Sample::UpdateDescriptors()
{
    std::array<VkDescriptorImageInfo, 64> textures;
    for (uint32_t i = 0; i < 64; ++i)
        textures[i] = {VK_NULL_HANDLE, textures_[i < textures_.size() ? i : 0].view,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet imageArray{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    imageArray.dstSet = descriptorSets_[0];
    imageArray.dstBinding = 1;
    imageArray.descriptorCount = 64;
    imageArray.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    imageArray.pImageInfo = textures.data();
    vkUpdateDescriptorSets(device_, 1, &imageArray, 0, nullptr);
    const Buffer *buffers[] = {&rayVertices_,        &rayIndices_,   &rayPrimitives_,
                               &rayInstancesBuffer_, &rayMaterials_, &points_};
    const VkDeviceSize sizes[] = {
        rayData_.vertices.size() * sizeof(RayVertex),      rayData_.indices.size() * 4,
        rayData_.primitives.size() * sizeof(RayPrimitive), rayInstances_.size() * sizeof(RayInstance),
        rayData_.materials.size() * sizeof(BSDFMaterial),   72 * sizeof(WorkshopLight)};
    for (uint32_t b = 0; b < 19; ++b)
    {
        if (b == 1)
            continue;
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSets_[0];
        write.dstBinding = b;
        write.descriptorCount = 1;
        VkDescriptorBufferInfo buf{};
        VkDescriptorImageInfo image{};
        VkWriteDescriptorSetAccelerationStructureKHR as{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
        if (b == 0)
        {
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            buf = {constants_.buffer, 0, sizeof(BSDFConstants)};
            write.pBufferInfo = &buf;
        }
        else if (b == 18)
        {
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            buf = {areaLights_.buffer, 0, pathLights_.lights.size() * sizeof(PathLight)};
            write.pBufferInfo = &buf;
        }
        else if (b == 3)
        {
            write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            as.accelerationStructureCount = 1;
            as.pAccelerationStructures = &tlas_.object;
            write.pNext = &as;
        }
        else if (b >= 4 && b <= 9)
        {
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            buf = {buffers[b - 4]->buffer, 0, sizes[b - 4]};
            write.pBufferInfo = &buf;
        }
        else
        {
            write.pImageInfo = &image;
            if (b == 2 || b == 14)
            {
                write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                image.sampler = b == 2 ? sampler_ : environmentSampler_;
            }
            else if (b >= 15)
            {
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                image.imageView = b == 15 ? hdr_[0].view : diagnostic_[b - 16].view;
                image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            }
            else
            {
                write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                image.imageView = b == 13 ? brdf_.view : environment_[b - 10].view;
                image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
    for (uint32_t p = 0; p < 4; ++p)
    {
        uint32_t source = p < 3 ? p : panel_.view >= 7 ? uint32_t(panel_.view - 6) : 0;
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
        if (kind <= 2 || kind == 6)
        {
            vi.vertexBindingDescriptionCount = 1;
            vi.pVertexBindingDescriptions = &binding;
            vi.vertexAttributeDescriptionCount = 4;
            vi.pVertexAttributeDescriptions = attrs;
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
                "Raytracing display pipeline");
    };
    graphics(postPipeline_, L"PostVertex", L"DeferredPostPixel", 5, true);
    CreateRayPipeline();
}

void Sample::CreateRayPipeline()
{
    auto shader = [&](const wchar_t *name)
    {
        auto bytes =
            ReadBinary(AssetPath(std::filesystem::path(L"Shaders") / (std::wstring(name) + L".spv")));
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = bytes.size();
        info.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
        VkShaderModule module;
        CheckVk(vkCreateShaderModule(device_, &info, nullptr, &module), "RT shader module");
        modules_.push_back(module);
        return module;
    };
    const wchar_t *names[] = {L"RayGeneration", L"ClosestHit", L"AlphaAnyHit", L"Miss", L"ShadowMiss"};
    const char *entries[] = {"RayGeneration", "ClosestHit", "AlphaAnyHit", "Miss", "ShadowMiss"};
    const VkShaderStageFlagBits stageTypes[] = {
        VK_SHADER_STAGE_RAYGEN_BIT_KHR, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
        VK_SHADER_STAGE_MISS_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR};
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    for (uint32_t i = 0; i < 5u; ++i)
    {
        VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage = stageTypes[i];
        stage.module = shader(names[i]);
        stage.pName = entries[i];
        stages.push_back(stage);
    }
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
    auto general = [&](uint32_t stage)
    {
        VkRayTracingShaderGroupCreateInfoKHR g{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
        g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        g.generalShader = stage;
        g.closestHitShader = g.anyHitShader = g.intersectionShader = VK_SHADER_UNUSED_KHR;
        groups.push_back(g);
    };
    general(0);
    general(3);
    if (RT_TECHNIQUE >= 2)
        general(4);
    auto hit = [&](uint32_t closest)
    {
        VkRayTracingShaderGroupCreateInfoKHR g{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
        g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        g.generalShader = g.intersectionShader = VK_SHADER_UNUSED_KHR;
        g.closestHitShader = closest;
        g.anyHitShader = 2;
        groups.push_back(g);
    };
    hit(1);
    if (RT_TECHNIQUE >= 2)
        hit(VK_SHADER_UNUSED_KHR);
    VkRayTracingPipelineCreateInfoKHR info{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    info.stageCount = uint32_t(stages.size());
    info.pStages = stages.data();
    info.groupCount = uint32_t(groups.size());
    info.pGroups = groups.data();
    info.maxPipelineRayRecursionDepth = 1;
    info.layout = pipelineLayout_;
    CheckVk(createRTP_(device_, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &info, nullptr, &rayPipeline_),
            "Create KHR ray tracing pipeline");
    auto align = [](VkDeviceSize n, VkDeviceSize a) { return (n + a - 1) / a * a; };
    VkDeviceSize handle = rtProperties_.shaderGroupHandleSize,
                 stride = align(handle, rtProperties_.shaderGroupHandleAlignment),
                 base = rtProperties_.shaderGroupBaseAlignment;
    if (stride > rtProperties_.maxShaderGroupStride)
        throw std::runtime_error("SBT stride exceeds device limit");
    VkDeviceSize missOffset = align(stride, base),
                 hitOffset = align(missOffset + stride * 2, base),
                 bytes = hitOffset + stride * 2;
    DestroyBuffer(shaderTable_);
    shaderTable_ = CreateBuffer(bytes + base, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    VkDeviceAddress address = Address(shaderTable_), aligned = align(address, base);
    VkDeviceSize offset = aligned - address;
    std::vector<uint8_t> handles(groups.size() * handle);
    CheckVk(
        getShaderHandles_(device_, rayPipeline_, 0, uint32_t(groups.size()), handles.size(), handles.data()),
        "SBT shader handles");
    auto *mapped = static_cast<uint8_t *>(shaderTable_.mapped) + offset;
    std::memset(shaderTable_.mapped, 0, size_t(bytes + base));
    std::memcpy(mapped, handles.data(), size_t(handle));
    uint32_t count = 2;
    for (uint32_t i = 0; i < count; ++i)
    {
        std::memcpy(mapped + missOffset + i * stride, handles.data() + (1 + i) * handle, size_t(handle));
        std::memcpy(mapped + hitOffset + i * stride, handles.data() + (1 + count + i) * handle,
                    size_t(handle));
    }
    Flush(shaderTable_);
    raygenRegion_ = {aligned, align(stride, base), align(stride, base)};
    missRegion_ = {aligned + missOffset, stride, stride * count};
    hitRegion_ = {aligned + hitOffset, stride, stride * count};
    callableRegion_ = {};
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
void Sample::OnUpdate(float seconds)
{
    if (auto p = panel_.TestCapture(); !p.empty())
        OnScreenshot(p);
    ImGui_ImplWin32_NewFrame();
    panel_.QueueTestInput();
    ImGui::NewFrame();
    panel_.Draw(workshop_, "Vulkan");
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
    if (IsIconic(window_))
        return;
    CheckVk(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "Frame completion");
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

    if (builtScene_ != panel_.scene || builtPreset_ != panel_.preset || builtOccluder_ != panel_.occluder)
        BuildTlas();
    UpdateDescriptors();
    if (panel_.materialsDirty) {
        rayData_ = MakeBSDFSceneData(workshop_);
        std::memcpy(rayMaterials_.mapped, rayData_.materials.data(), rayData_.materials.size() * sizeof(BSDFMaterial));
        Flush(rayMaterials_); panel_.materialsDirty = false;
    }
    auto c = panel_.Constants(workshop_, width_, height_, 0, pathLights_.count);
    if (!havePrevious_ || panel_.resetRequested || std::memcmp(&c, previousConstants_.data(), sizeof(c)) != 0) {
        samples_ = 0; panel_.resetRequested = false;
        std::memcpy(previousConstants_.data(), &c, sizeof(c)); havePrevious_ = true;
        Trace("Path accumulation reset");
    }
    c.controls.z = samples_;
    const bool traceSample = (!captureOnly_ || samples_ == 0) && (!panel_.paused || samples_ == 0) && (!panel_.targetSpp || samples_ < panel_.targetSpp);
    std::memcpy(constants_.mapped, &c, sizeof(c));
    Flush(constants_);
    const auto &lights = workshop_.scenes[panel_.scene].lights;
    if (!lights.empty())
        std::memcpy(points_.mapped, lights.data(), lights.size() * sizeof(WorkshopLight));
    Flush(points_);
    for (uint32_t i = 0; i < 4; ++i)
    {
        PostConstants p{};
        p.settings = {panel_.exposure, panel_.threshold, panel_.strength, 0};
        p.dimensions = {width_, height_, (width_ + 1) / 2, (height_ + 1) / 2};
        p.flags = {0, 0, 0, 0};
        std::memcpy(static_cast<uint8_t *>(postConstants_.mapped) + i * constantStride_, &p, sizeof(p));
    }
    Flush(postConstants_);
    CheckVk(vkResetCommandBuffer(commands_, 0), "Frame reset");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CheckVk(vkBeginCommandBuffer(commands_, &begin), "Frame begin");
    Move(hdr_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[0], VK_IMAGE_LAYOUT_GENERAL);
    for (uint32_t i = 0; i < 2; ++i)
        Move(diagnostic_[i].image, VK_IMAGE_ASPECT_COLOR_BIT, diagnosticLayouts_[i], VK_IMAGE_LAYOUT_GENERAL);
    VkMemoryBarrier2 pathBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    pathBarrier.srcStageMask = pathBarrier.dstStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    pathBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    pathBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    VkDependencyInfo pathDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    pathDependency.memoryBarrierCount = 1; pathDependency.pMemoryBarriers = &pathBarrier;
    vkCmdPipelineBarrier2(commands_, &pathDependency);
    vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayPipeline_);
    vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout_, 0, 1,
                            descriptorSets_.data(), 0, nullptr);
    if (traceSample) { traceRays_(commands_, &raygenRegion_, &missRegion_, &hitRegion_, &callableRegion_, width_, height_, 1); ++samples_; }
    panel_.samples = samples_;
    Move(hdr_[0].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    for (uint32_t i = 1; i < 4; ++i)
        Move(hdr_[i].image, VK_IMAGE_ASPECT_COLOR_BIT, hdrLayouts_[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
}
void Sample::OnResize(uint32_t width, uint32_t height)
{
    if (!width || !height || IsIconic(window_))
        return;
    CheckVk(vkDeviceWaitIdle(device_), "Resize completion");
    DestroyPipelines();
    DestroySwapchain();
    for (auto &g : diagnostic_)
        DestroyImage(g);
    for (auto &h : hdr_)
        DestroyImage(h);
    width_ = width;
    height_ = height;
    havePrevious_ = false;
    CreateSwapchain();
    CreateSizeResources();
    CreatePipeline();
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
    const uint32_t sizes[] = {16, 16, 16};
    for (uint32_t i = 0; i < 3; ++i)
    {
        auto image = i == 0 ? hdr_[0].image : diagnostic_[i - 1].image;
        auto &layout = i == 0 ? hdrLayouts_[0] : diagnosticLayouts_[i - 1];
        auto before = layout;
        uint32_t w = width_, h = height_;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
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

        DestroyBuffer(readback);
    }
    CaptureMetadata(directory, metadata);
}
void Sample::OnDestroy() noexcept
{
    panel_.navigation.Reset();
    try
    {
        if (device_)
            CheckVk(vkDeviceWaitIdle(device_), "Shutdown completion");
    }
    catch (const std::exception &e)
    {
        ValidationError(e.what());
    }
    DestroyImGui();
    if (device_)
    {
        DestroyPipelines();
        DestroyAcceleration(tlas_);
        for (auto &a : blas_)
            DestroyAcceleration(a);
        blas_.clear();
        for (auto *b : {&rayVertices_, &rayIndices_, &rayPrimitives_, &rayMaterials_, &rayInstancesBuffer_,
                        &shaderTable_, &areaLights_})
            DestroyBuffer(*b);
        for (auto &d : diagnostic_)
            DestroyImage(d);
        DestroySwapchain();
        for (auto &t : textures_)
            DestroyImage(t);
        textures_.clear();
        for (auto &h : hdr_)
            DestroyImage(h);
        for (auto &e : environment_)
            DestroyImage(e);
        DestroyImage(brdf_);
        for (auto *b : {&constants_, &points_, &postConstants_, &staging_, &capture_})
            DestroyBuffer(*b);
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
void Sample::DestroyPipelines()
{

    if (postPipeline_)
        vkDestroyPipeline(device_, postPipeline_, nullptr);
    postPipeline_ = VK_NULL_HANDLE;
    if (rayPipeline_)
        vkDestroyPipeline(device_, rayPipeline_, nullptr);
    rayPipeline_ = VK_NULL_HANDLE;
    for (auto m : modules_)
        vkDestroyShaderModule(device_, m, nullptr);
    modules_.clear();
}
void Sample::OnScreenshot(const std::filesystem::path &path)
{
    // A final-frame test can minimize immediately before requesting its capture.
    // Keep dispatching Win32 messages so its restore timer can run; do not create
    // swapchain/storage images for the minimized surface extent.
    while (IsWindow(window_) && IsIconic(window_))
    {
        MSG message{};
        const auto result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0)
            throw std::runtime_error("Window closed while waiting to capture a restored surface");
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    RECT client{};
    if (!GetClientRect(window_, &client) || client.right <= 0 || client.bottom <= 0)
        throw std::runtime_error("Screenshot requires a nonempty client area");
    if (width_ != uint32_t(client.right) || height_ != uint32_t(client.bottom))
        OnResize(uint32_t(client.right), uint32_t(client.bottom));
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
