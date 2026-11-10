#include "Sample.h"
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
    panel_.Initialize(options);
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
        if (!features.dynamicRendering || !features.synchronization2)
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
            if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present && score > bestScore)
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
    const char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features.dynamicRendering = VK_TRUE;
    features.synchronization2 = VK_TRUE;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &features;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;
    CheckVk(vkCreateDevice(physical_, &deviceInfo, nullptr, &device_), "vkCreateDevice");
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
    CreateResources();
    CreatePipeline();
    CreateImGui();
}

bool Sample::OnMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LRESULT &result)
{
    if (!ImGui::GetCurrentContext() || !win32Ui_)
        return false;
    result = ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
    const auto &io = ImGui::GetIO();
    return result != 0 || (io.WantCaptureKeyboard &&
                           (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN ||
                            message == WM_SYSKEYUP || message == WM_CHAR));
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

Sample::ImageResource Sample::CreateImage(uint32_t width, uint32_t height, VkFormat format,
                                          VkImageUsageFlags usage, VkImageAspectFlags aspect)
{
    ImageResource resource;
    resource.aspect = format == VK_FORMAT_D24_UNORM_S8_UINT
                          ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                          : aspect;
    try
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {width, height, 1};
        info.mipLevels = 1;
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
        view.subresourceRange = {aspect, 0, 1, 0, 1};
        CheckVk(vkCreateImageView(device_, &view, nullptr, &resource.view), "vkCreateImageView");
        return resource;
    }
    catch (...)
    {
        DestroyImage(resource);
        throw;
    }
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

void Sample::OnScreenshot(const std::filesystem::path &path)
{
    // Read back an acquired image before presenting it; never access an image owned by presentation.
    capturePath_ = path;
    OnRender();
    if (!capturePath_.empty())
    {
        OnRender();
        if (!capturePath_.empty())
            throw std::runtime_error("Swapchain changed during screenshot capture");
    }
    if (panel_.captureGpu)
        CaptureData(path);
}

void Sample::OnUpdate(float)
{
    if (const auto path = panel_.TestCapture(); !path.empty())
        OnScreenshot(path);
    ImGui_ImplWin32_NewFrame();
    panel_.QueueTestInput();
    ImGui::NewFrame();
    panel_.Draw(width_, height_, "Vulkan");
    ImGui::Render();
}

void Sample::TransitionImage(ImageResource &image, VkImageLayout layout)
{
    // Conservative full-frame dependencies keep the educational resource transitions explicit.
    ImageBarrier(commands_, image.image, image.aspect, image.layout, layout,
                 image.layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_NONE
                                                           : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 image.layout == VK_IMAGE_LAYOUT_UNDEFINED
                     ? 0
                     : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    image.layout = layout;
}
void Sample::Bind(uint32_t slot)
{
    vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &sets_[slot],
                            0, nullptr);
}
void Sample::CreateResources()
{
    using namespace tutorial::depth;
    panel_.Layout(width_, height_);
    sceneWidth_ = std::max(1u, uint32_t(panel_.cells[0].w));
    sceneHeight_ = std::max(1u, uint32_t(panel_.cells[0].h));
    shadowSize_ = panel_.shadow ? panel_.settings.resolution : 1;
    const VkFormat formats[] = {VK_FORMAT_D16_UNORM, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT};
    for (int i = 0; i < 3; ++i)
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physical_, formats[i], &props);
        VkFormatFeatureFlags required =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        panel_.supported[i] = (props.optimalTilingFeatures & required) == required;
    }
    if (!panel_.supported[2] || !panel_.supported[panel_.settings.format])
        throw std::runtime_error("Requested depth format lacks depth-attachment / sampled-image support");
    if (!descriptorLayout_)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physical_, &props);
        size_t alignment = size_t(props.limits.minUniformBufferOffsetAlignment);
        constantStride_ = (sizeof(Constants) + alignment - 1) / alignment * alignment;
        constants_ = CreateBuffer(constantStride_ * 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        VkDescriptorSetLayoutBinding bindings[9]{};
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        for (uint32_t i = 0; i < 8; ++i)
            bindings[i + 1] = {i == 0 ? 1 : i + 2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1,
                               VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = 9;
        layout.pBindings = bindings;
        CheckVk(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &descriptorLayout_),
                "Depth descriptor layout");
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &descriptorLayout_;
        CheckVk(vkCreatePipelineLayout(device_, &pl, nullptr, &pipelineLayout_), "Depth pipeline layout");
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16},
                                        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 128}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 16;
        pool.poolSizeCount = 2;
        pool.pPoolSizes = sizes;
        CheckVk(vkCreateDescriptorPool(device_, &pool, nullptr, &descriptorPool_), "Depth descriptor pool");
        std::array<VkDescriptorSetLayout, 16> layouts;
        layouts.fill(descriptorLayout_);
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = descriptorPool_;
        allocate.descriptorSetCount = 16;
        allocate.pSetLayouts = layouts.data();
        CheckVk(vkAllocateDescriptorSets(device_, &allocate, sets_.data()), "Depth descriptor sets");
    }
    for (auto &t : textures_)
        DestroyImage(t);
    for (auto &t : exports_)
        DestroyImage(t);
    for (int i = 0; i < 8; ++i)
    {
        bool depth = i == 0 || i == 3 || i == 6;
        uint32_t w = i >= 6 ? shadowSize_ : sceneWidth_, h = i >= 6 ? shadowSize_ : sceneHeight_;
        VkFormat f = depth                ? formats[i == 6 || panel_.shadow ? 2 : panel_.settings.format]
                     : i == 7             ? VK_FORMAT_R32_SFLOAT
                     : (i == 1 || i == 4) ? VK_FORMAT_R32G32B32A32_SFLOAT
                                          : VK_FORMAT_R8G8B8A8_UNORM;
        textures_[i] =
            CreateImage(w, h, f,
                        VK_IMAGE_USAGE_SAMPLED_BIT | (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                                            : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
                        depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
    }
    for (int i = 0; i < 3; ++i)
        exports_[i] = CreateImage(i == 2 ? shadowSize_ : sceneWidth_, i == 2 ? shadowSize_ : sceneHeight_,
                                  VK_FORMAT_R32G32B32A32_SFLOAT,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT);
    UpdateDescriptors();
    activeFormat_ = panel_.settings.format;
    activeResolution_ = panel_.settings.resolution;
    panel_.dirty = true;
}
void Sample::UpdateDescriptors()
{
    VkDescriptorImageInfo images[8]{};
    for (int i = 0; i < 8; ++i)
    {
        images[i].imageView = textures_[i].view;
        images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    for (uint32_t p = 0; p < 16; ++p)
    {
        VkDescriptorBufferInfo buffer{constants_.buffer, p * constantStride_,
                                      sizeof(tutorial::depth::Constants)};
        VkWriteDescriptorSet writes[9]{};
        for (auto &w : writes)
        {
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = sets_[p];
            w.descriptorCount = 1;
        }
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &buffer;
        for (uint32_t i = 0; i < 8; ++i)
        {
            writes[i + 1].dstBinding = i == 0 ? 1 : i + 2;
            writes[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[i + 1].pImageInfo = &images[i];
        }
        vkUpdateDescriptorSets(device_, 9, writes, 0, nullptr);
    }
}
void Sample::CreatePipeline()
{
    std::vector<VkShaderModule> shaders;
    auto shader = [&](const wchar_t *file)
    {
        auto bytes = ReadBinary(AssetPath(file));
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = bytes.size();
        info.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
        VkShaderModule result;
        CheckVk(vkCreateShaderModule(device_, &info, nullptr, &result), "Depth shader");
        shaders.push_back(result);
        return result;
    };
    try
    {
        auto vs = shader(L"Shaders/DepthVertex.spv"), ps = shader(L"Shaders/DepthPixel.spv"),
             fs = shader(L"Shaders/DepthFullscreen.spv"), sp = shader(L"Shaders/ShadowLinear.spv"),
             dp = shader(L"Shaders/DepthDisplay.spv");
        VkVertexInputBindingDescription binding{0, sizeof(tutorial::depth::Vertex),
                                                VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                                     {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
                                                     {2, 0, VK_FORMAT_R32_SFLOAT, 24}};
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = 3;
        input.pVertexAttributeDescriptions = attrs;
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
        VkPipelineColorBlendAttachmentState attachments[2]{};
        for (auto &a : attachments)
            a.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 2;
        blend.pAttachments = attachments;
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dyn;
        VkFormat formats[] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32G32B32A32_SFLOAT};
        const VkFormat depths[] = {VK_FORMAT_D16_UNORM, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT};
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 2;
        rendering.pColorAttachmentFormats = formats;
        rendering.depthAttachmentFormat = depths[panel_.shadow ? 2 : panel_.settings.format];
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (auto &s : stages)
            s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "VSMain";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = ps;
        stages[1].pName = "PSMain";
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &input;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &vp;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &ms;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout_;
        auto make = [&](VkPipeline &pipeline)
        {
            if (pipeline)
                vkDestroyPipeline(device_, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
            CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                    "Depth graphics pipeline");
        };
        for (int p = 0; p < 2; ++p)
        {
            depth.depthCompareOp = p || panel_.shadow ? VK_COMPARE_OP_GREATER : VK_COMPARE_OP_LESS;
            make(scenePipelines_[p]);
        }
        rendering.colorAttachmentCount = blend.attachmentCount = 1;
        formats[0] = VK_FORMAT_R32_SFLOAT;
        rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        stages[1].module = sp;
        make(shadowPipeline_);
        input.vertexAttributeDescriptionCount = input.vertexBindingDescriptionCount = 0;
        depth.depthTestEnable = depth.depthWriteEnable = VK_FALSE;
        rendering.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        stages[0].module = fs;
        stages[1].module = dp;
        formats[0] = colorFormat_;
        make(displayPipeline_);
        formats[0] = VK_FORMAT_R32G32B32A32_SFLOAT;
        make(exportPipeline_);
    }
    catch (...)
    {
        for (auto s : shaders)
            vkDestroyShaderModule(device_, s, nullptr);
        throw;
    }
    for (auto s : shaders)
        vkDestroyShaderModule(device_, s, nullptr);
}
void Sample::RenderLessons()
{
    auto viewport = [&](uint32_t w, uint32_t h)
    {
        VkViewport vp{0, float(h), float(w), -float(h), 0, 1};
        VkRect2D sc{{0, 0}, {w, h}};
        vkCmdSetViewport(commands_, 0, 1, &vp);
        vkCmdSetScissor(commands_, 0, 1, &sc);
    };
    auto attachment = [&](int slot, bool depth, float clear)
    {
        VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        a.imageView = textures_[slot].view;
        a.imageLayout = depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                              : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (depth)
            a.clearValue.depthStencil = {clear, 0};
        else
            a.clearValue.color = {{clear, 0, 0, 0}};
        return a;
    };
    TransitionImage(textures_[6], VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    TransitionImage(textures_[7], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    auto sd = attachment(6, true, 1), sl = attachment(7, false, -1);
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = {shadowSize_, shadowSize_};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &sl;
    rendering.pDepthAttachment = &sd;
    vkCmdBeginRendering(commands_, &rendering);
    viewport(shadowSize_, shadowSize_);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commands_, 0, 1, &vertices_.buffer, &offset);
    Bind(2);
    vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
    if (panel_.shadow)
        vkCmdDraw(commands_, drawCount_, 1, 0, 0);
    vkCmdEndRendering(commands_);
    TransitionImage(textures_[6], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    TransitionImage(textures_[7], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    for (int p = 0; p < 2; ++p)
    {
        for (int i = 0; i < 3; ++i)
            TransitionImage(textures_[p * 3 + i], i == 0 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                         : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo colors[] = {attachment(p * 3 + 2, false, 0),
                                              attachment(p * 3 + 1, false, 0)};
        auto dep = attachment(p * 3, true, p || panel_.shadow ? 0.f : 1.f);
        rendering.renderArea.extent = {sceneWidth_, sceneHeight_};
        rendering.colorAttachmentCount = 2;
        rendering.pColorAttachments = colors;
        rendering.pDepthAttachment = &dep;
        vkCmdBeginRendering(commands_, &rendering);
        viewport(sceneWidth_, sceneHeight_);
        Bind(p);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelines_[p]);
        vkCmdDraw(commands_, drawCount_, 1, 0, 0);
        vkCmdEndRendering(commands_);
        for (int i = 0; i < 3; ++i)
            TransitionImage(textures_[p * 3 + i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}
void Sample::CaptureData(const std::filesystem::path &path)
{
    CheckVk(vkQueueWaitIdle(queue_), "Readback wait");
    CheckVk(vkResetCommandBuffer(commands_, 0), "Readback reset");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(commands_, &begin), "Readback begin");
    std::array<Buffer, 3> buffers;
    int count = panel_.shadow ? 3 : 2;
    for (int p = 0; p < count; ++p)
    {
        uint32_t w = p == 2 ? shadowSize_ : sceneWidth_, h = p == 2 ? shadowSize_ : sceneHeight_;
        TransitionImage(exports_[p], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = exports_[p].view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = {w, h};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(commands_, &rendering);
        VkViewport vp{0, float(h), float(w), -float(h), 0, 1};
        VkRect2D sc{{0, 0}, {w, h}};
        vkCmdSetViewport(commands_, 0, 1, &vp);
        vkCmdSetScissor(commands_, 0, 1, &sc);
        Bind(10 + p);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, exportPipeline_);
        vkCmdDraw(commands_, 3, 1, 0, 0);
        vkCmdEndRendering(commands_);
        TransitionImage(exports_[p], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        buffers[p] = CreateBuffer(VkDeviceSize(w) * h * sizeof(tutorial::depth::Pixel),
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(commands_, exports_[p].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               buffers[p].buffer, 1, &copy);
    }
    VkMemoryBarrier2 host{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &host;
    vkCmdPipelineBarrier2(commands_, &dep);
    CheckVk(vkEndCommandBuffer(commands_), "Readback end");
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = commands_;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    CheckVk(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE), "Readback submit");
    CheckVk(vkQueueWaitIdle(queue_), "Readback complete");
    for (int p = 0; p < count; ++p)
    {
        uint32_t w = p == 2 ? shadowSize_ : sceneWidth_, h = p == 2 ? shadowSize_ : sceneHeight_;
        if (!buffers[p].coherent)
        {
            VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            r.memory = buffers[p].memory;
            r.size = VK_WHOLE_SIZE;
            CheckVk(vkInvalidateMappedMemoryRanges(device_, 1, &r), "Depth invalidate");
        }
        auto &output = p == 2 ? lightPixels_ : pixels_[p];
        output.resize(size_t(w) * h);
        std::memcpy(output.data(), buffers[p].mapped, output.size() * sizeof(tutorial::depth::Pixel));
        DestroyBuffer(buffers[p]);
    }
    panel_.UpdateProbes(pixels_, sceneWidth_, sceneHeight_, lightPixels_);
    if (!path.empty())
        tutorial::depth::SaveMeasurements(path, panel_.settings, panel_.shadow, sceneWidth_, sceneHeight_,
                                          pixels_, lightPixels_, "Vulkan");
}
void Sample::OnResize(uint32_t width, uint32_t height)
{
    CheckVk(vkDeviceWaitIdle(device_), "Resize wait");
    width_ = width;
    height_ = height;
    const auto old = colorFormat_;
    DestroySwapchain();
    CreateSwapchain();
    CreateResources();
    if (old != colorFormat_)
    {
        CreatePipeline();
        if (uiPipeline_)
        {
            vkDestroyPipeline(device_, uiPipeline_, nullptr);
            uiPipeline_ = VK_NULL_HANDLE;
            CreateUiPipeline();
        }
    }
}
void Sample::DestroyLessons()
{
    for (auto &t : textures_)
        DestroyImage(t);
    for (auto &t : exports_)
        DestroyImage(t);
    for (auto &p : scenePipelines_)
    {
        if (p)
            vkDestroyPipeline(device_, p, nullptr);
        p = VK_NULL_HANDLE;
    }
    for (auto p : {shadowPipeline_, displayPipeline_, exportPipeline_})
        if (p)
            vkDestroyPipeline(device_, p, nullptr);
    shadowPipeline_ = displayPipeline_ = exportPipeline_ = VK_NULL_HANDLE;
}
void Sample::OnDestroy() noexcept
{
    if (device_)
    {
        if (vkDeviceWaitIdle(device_) != VK_SUCCESS)
            ValidationError("Depth shutdown wait failed");
        DestroyImGui();
        DestroyLessons();
        DestroyBuffer(vertices_);
        DestroyBuffer(constants_);
        DestroyBuffer(capture_);
        if (descriptorPool_)
            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        if (pipelineLayout_)
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        if (descriptorLayout_)
            vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);
        DestroySwapchain();
        if (fence_)
            vkDestroyFence(device_, fence_, nullptr);
        if (imageAvailable_)
            vkDestroySemaphore(device_, imageAvailable_, nullptr);
        if (commandPool_)
            vkDestroyCommandPool(device_, commandPool_, nullptr);
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    DestroyImGui();
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
}
void Sample::OnRender()
{
    CheckVk(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "Frame fence");

    using namespace tutorial::depth;
    if (activeFormat_ != panel_.settings.format || activeResolution_ != panel_.settings.resolution)
    {
        CreateResources();
        CreatePipeline();
    }
    const auto vertices = Scene(panel_.settings, panel_.shadow);
    DestroyBuffer(vertices_);
    vertices_ =
        CreateBuffer(vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices.data());
    drawCount_ = uint32_t(vertices.size());
    for (int i = 0; i < 16; ++i)
    {
        data_[i] = MakeConstants(panel_.settings, panel_.shadow, i % 2, sceneWidth_, sceneHeight_);
        if (i == 2)
        {
            data_[i].view = data_[i].lightView;
            data_[i].projection = data_[i].lightProjection;
        }
        if (i >= 3 && i < 9)
            data_[i].display = {float((i - 3) % 2), float((i - 3) / 2), data_[i].display.z,
                                data_[i].display.w};
        if (i >= 10)
            data_[i].display = {float(i == 11), float(i == 12 ? 4 : 3), data_[i].display.z,
                                data_[i].display.w};
        std::memcpy(static_cast<char *>(constants_.mapped) + constantStride_ * i, &data_[i],
                    sizeof(Constants));
    }
    Flush(constants_);
    uint32_t index = 0;
    const auto acquire =
        vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable_, VK_NULL_HANDLE, &index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
    {
        OnResize(width_, height_);
        return;
    }
    if (acquire != VK_SUBOPTIMAL_KHR)
        CheckVk(acquire, "vkAcquireNextImageKHR");
    if (!capturePath_.empty())
    {
        if (!canCapture_)
            throw std::runtime_error("Swapchain does not support screenshot readback");
        capture_ = CreateBuffer(VkDeviceSize(width_) * height_ * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }
    CheckVk(vkResetFences(device_, 1, &fence_), "vkResetFences");
    CheckVk(vkResetCommandBuffer(commands_, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(commands_, &begin), "vkBeginCommandBuffer");

    RenderLessons();
    ImageBarrier(commands_, images_[index], VK_IMAGE_ASPECT_COLOR_BIT,
                 presented_[index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = views_[index];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{.004f, .008f, .015f, 1}};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = {width_, height_};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(commands_, &rendering);
    for (int i = 0; i < 6; ++i)
    {
        auto r = panel_.cells[i];
        VkViewport v{r.x, r.y + r.h, r.w, -r.h, 0, 1};
        VkRect2D sc{{int32_t(r.x), int32_t(r.y)}, {uint32_t(r.w), uint32_t(r.h)}};
        vkCmdSetViewport(commands_, 0, 1, &v);
        vkCmdSetScissor(commands_, 0, 1, &sc);
        Bind(3 + i);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, displayPipeline_);
        vkCmdDraw(commands_, 3, 1, 0, 0);
    }
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
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
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
    if (panel_.dirty)
    {
        CaptureData();
        panel_.dirty = false;
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquire == VK_SUBOPTIMAL_KHR)
        OnResize(width_, height_);
}
