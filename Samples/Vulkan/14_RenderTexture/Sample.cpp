#include "Sample.h"
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
    scene_ = LoadLightingScene(options.scene.empty() ? AssetPath(L"Assets/Scenes/PostProcess/Exhibition.json")
                                                     : options.scene);
    model_ = scene_.objects.front().model;
    secondaryScene_ = SecondaryScene(scene_);
    panel_.Initialize(scene_, options, POST_TECHNIQUE);
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
    const char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features.dynamicRendering = VK_TRUE;
    features.synchronization2 = VK_TRUE;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkPhysicalDeviceFeatures basic{};
    basic.shaderStorageImageExtendedFormats = POST_TECHNIQUE >= 15 ? VK_TRUE : VK_FALSE;
    deviceInfo.pEnabledFeatures = &basic;
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
    CreatePipeline();
    CreatePostResources();
    CreateScene(options);

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
    CreateDepth();
}
void Sample::DestroySwapchain()
{
    DestroyImage(depth_);
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
void Sample::OnUpdate(float seconds)
{
    if (const auto path = panel_.TestCapture(); !path.empty())
        OnScreenshot(path);
    ImGui_ImplWin32_NewFrame();
    panel_.QueueTestInput();
    ImGui::NewFrame();
    delta_ = panel_.fixedDelta > 0 ? panel_.fixedDelta : std::clamp(seconds - lastSeconds_, 0.f, .05f);
    lastSeconds_ = seconds;
    panel_.Draw(width_, height_, model_, "Vulkan");
    panel_.navigation.Update(window_, seconds, scene_.camera, scene_.target);
    ImGui::Render();
}

void Sample::OnRender()
{
    CheckVk(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "Frame fence");

    UpdateLabDescriptors();
    UpdatePostDescriptors();
    for (uint32_t bank = 0; bank < 2; ++bank)
        for (size_t i = 0; i < objects_.size(); ++i)
        {
            auto c = MakeLabConstants(
                bank ? 512 : width_ - panel_.SceneLeft(width_), bank ? 512 : height_,
                bank ? secondaryScene_ : scene_, i, 13, panel_.view, panel_.environmentIntensity, panel_.yaw,
                panel_.directIntensity, 1, panel_.shadows, panel_.pcf, panel_.bias, panel_.occluder,
                bank ? nullptr : panel_.navigation.Position(), bank ? nullptr : panel_.navigation.Target());
            c.lighting.shading = {float(bank ? 512 : width_ - panel_.SceneLeft(width_)),
                                  float(bank ? 512 : height_), float(bank ? 0 : panel_.SceneLeft(width_)), 0};
            std::memcpy(static_cast<uint8_t *>(constants_.mapped) +
                            (bank * objects_.size() + i) * constantStride_,
                        &c, sizeof(c));
        }
    Flush(constants_);
    PostConstants post{};
    post.settings = {panel_.exposure, panel_.threshold, panel_.strength, float(panel_.postView)};
    post.dimensions = {width_, height_, (width_ + 1) / 2, (height_ + 1) / 2};
    post.emitter = {.2f, .08f, -1.4f, .008f};
    bool reset = panel_.resetRequested;
    float dt = captureOnly_ ? 0 : panel_.paused ? (panel_.stepRequested ? 1.f / 60 : 0) : delta_;
    post.simulation = {dt, panel_.gravity, float(panel_.particleCount), reset ? 1.f : 0.f};
    post.flags = {POST_TECHNIQUE >= 15 && panel_.bloom ? 1u : 0u, 0, uint32_t(panel_.testImage), 0};
    for (uint32_t pass = 0; pass < 5; ++pass)
    {
        post.flags.y = pass == 2 ? 1u : 2u;
        std::memcpy(static_cast<uint8_t *>(postConstants_.mapped) + pass * constantStride_, &post,
                    sizeof(post));
    }
    Flush(postConstants_);
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

    if constexpr (POST_TECHNIQUE >= 16)
    {
        if (reset || dt > 0)
        {
            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.buffer = particles_.buffer;
            barrier.size = VK_WHOLE_SIZE;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.srcStageMask =
                particlesReady_ ? VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT : VK_PIPELINE_STAGE_2_NONE;
            barrier.srcAccessMask = particlesReady_ ? VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT : 0;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask =
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.bufferMemoryBarrierCount = 1;
            dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(commands_, &dependency);
            vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, particleCompute_);
            vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, particleLayout_, 0, 1,
                                    &postSets_[4], 0, nullptr);
            vkCmdDispatch(commands_, (reset ? 65536u : uint32_t(panel_.particleCount) + 255) / 256, 1, 1);
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
            vkCmdPipelineBarrier2(commands_, &dependency);
            particlesReady_ = true;
            simulationSteps_ = reset ? 0 : simulationSteps_ + 1;
        }
    }
    panel_.resetRequested = panel_.stepRequested = false;
    if (panel_.postTest && panel_.frame == 12 && simulationSteps_ != 1)
        throw std::runtime_error("Particle Step UI failed");
    if (panel_.postTest && panel_.frame == 22 && simulationSteps_ != 0)
        throw std::runtime_error("Particle Reset UI failed");
    if (panel_.postTest && panel_.frame == 32 && (panel_.paused || simulationSteps_ < 2))
        throw std::runtime_error("Particle Pause UI failed");
    if constexpr (LAB_TECHNIQUE == 13)
    {
        ImageBarrier(commands_, shadow_.image, VK_IMAGE_ASPECT_DEPTH_BIT,
                     shadowReady_ ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     shadowReady_ ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_NONE,
                     shadowReady_ ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : 0,
                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                         VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView = shadow_.view;
        attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.clearValue.depthStencil = {1, 0};
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = {2048, 2048};
        rendering.layerCount = 1;
        rendering.pDepthAttachment = &attachment;
        vkCmdBeginRendering(commands_, &rendering);
        VkViewport viewport{0, 2048, 2048, -2048, 0, 1};
        VkRect2D scissor{{0, 0}, {2048, 2048}};
        vkCmdSetViewport(commands_, 0, 1, &viewport);
        vkCmdSetScissor(commands_, 0, 1, &scissor);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
        for (size_t i = 0; i < objects_.size(); ++i)
            if (scene_.objects[i].castsShadow)
            {
                uint32_t offset = static_cast<uint32_t>(i * constantStride_);
                vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                                        &descriptorSets_[i], 1, &offset);
                VkDeviceSize zero = 0;
                vkCmdBindVertexBuffers(commands_, 0, 1, &objects_[i].vertices.buffer, &zero);
                vkCmdBindIndexBuffer(commands_, objects_[i].indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(commands_, objects_[i].count, 1, 0, 0, 0);
            }
        vkCmdEndRendering(commands_);
        ImageBarrier(commands_, shadow_.image, VK_IMAGE_ASPECT_DEPTH_BIT,
                     VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                         VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                     VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        shadowReady_ = true;
    }

    DrawHdrScene(1, 1);
    DrawHdrScene(0, 0);
    if constexpr (POST_TECHNIQUE >= 15)
        for (uint32_t pass = 0; pass < 3; ++pass)
        {
            PostTransition(pass + 2, VK_IMAGE_LAYOUT_GENERAL);
            vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, bloomPipelines_[pass]);
            vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_COMPUTE, postLayout_, 0, 1,
                                    &postSets_[pass + 1], 0, nullptr);
            vkCmdDispatch(commands_, ((width_ + 1) / 2 + 7) / 8, ((height_ + 1) / 2 + 7) / 8, 1);
            PostTransition(pass + 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    // Acquisition waits at the stage that first writes the acquired swapchain image.
    ImageBarrier(commands_, images_[index], VK_IMAGE_ASPECT_COLOR_BIT,
                 presented_[index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = views_[index];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = {width_, height_};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(commands_, &rendering);
    VkViewport viewport{0, float(height_), float(width_), -float(height_), 0, 1};
    VkRect2D scissor{{0, 0}, {width_, height_}};
    vkCmdSetViewport(commands_, 0, 1, &viewport);
    vkCmdSetScissor(commands_, 0, 1, &scissor);
    vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline_);
    vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout_, 0, 1, &postSets_[0], 0,
                            nullptr);
    vkCmdDraw(commands_, 3, 1, 0, 0);
    vkCmdEndRendering(commands_);
    ImageBarrier(commands_, images_[index], VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    vkCmdBeginRendering(commands_, &rendering);
    if (panel_.visible)
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
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquire == VK_SUBOPTIMAL_KHR)
        OnResize(width_, height_);
}
void Sample::OnResize(uint32_t width, uint32_t height)
{
    CheckVk(vkDeviceWaitIdle(device_), "Resize wait");
    width_ = width;
    height_ = height;
    const auto previousFormat = colorFormat_;
    DestroySwapchain();
    CreateSwapchain();
    CreatePostTargets();
    if (colorFormat_ != previousFormat)
    {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
        if (skyPipeline_)
            vkDestroyPipeline(device_, skyPipeline_, nullptr);
        skyPipeline_ = VK_NULL_HANDLE;
        if (shadowPipeline_)
            vkDestroyPipeline(device_, shadowPipeline_, nullptr);
        shadowPipeline_ = VK_NULL_HANDLE;
        if (monitorPipeline_)
            vkDestroyPipeline(device_, monitorPipeline_, nullptr);
        monitorPipeline_ = VK_NULL_HANDLE;
        if (particlePipeline_)
            vkDestroyPipeline(device_, particlePipeline_, nullptr);
        particlePipeline_ = VK_NULL_HANDLE;
        CreatePipeline();
        DestroyPostPipelines();
        CreatePostPipelines();
        if (uiPipeline_)
        {
            vkDestroyPipeline(device_, uiPipeline_, nullptr);
            uiPipeline_ = VK_NULL_HANDLE;
            CreateUiPipeline();
        }
    }
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
void Sample::OnDestroy() noexcept
{
    panel_.navigation.Reset();
    if (device_)
    {
        const auto result = vkDeviceWaitIdle(device_);
        if (result != VK_SUCCESS)
            ValidationError("vkDeviceWaitIdle failed during shutdown");
        DestroyImGui();
        for (auto &o : objects_)
        {
            DestroyBuffer(o.vertices);
            DestroyBuffer(o.indices);
        }
        objects_.clear();
        // Orientation irradiance/specular alias Studio; destroy only owned handles.
        for (size_t e = 0; e < 2; ++e)
            for (auto &t : environments_[e])
                DestroyImage(t);
        DestroyImage(environments_[2][0]);
        environments_[2][1] = {};
        environments_[2][2] = {};
        DestroyImage(brdf_);
        DestroyImage(shadow_);
        if (skyPipeline_)
            vkDestroyPipeline(device_, skyPipeline_, nullptr);
        if (shadowPipeline_)
            vkDestroyPipeline(device_, shadowPipeline_, nullptr);
        if (environmentSampler_)
            vkDestroySampler(device_, environmentSampler_, nullptr);
        DestroyBuffer(vertices_);
        if (pipeline_)
            vkDestroyPipeline(device_, pipeline_, nullptr);
        if (pipelineLayout_)
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        if (vertexShader_)
            vkDestroyShaderModule(device_, vertexShader_, nullptr);
        if (pixelShader_)
            vkDestroyShaderModule(device_, pixelShader_, nullptr);
        DestroyBuffer(indices_);
        DestroyBuffer(constants_);
        DestroyBuffer(staging_);
        for (auto &texture : textures_)
            DestroyImage(texture);
        if (sampler_)
            vkDestroySampler(device_, sampler_, nullptr);
        if (descriptorPool_)
            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        if (descriptorLayout_)
            vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);
        DestroyBuffer(capture_);
        DestroyPostResources();
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
    DestroyImGui(); // Safe even when initialization stopped before the device was created.
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

void Sample::CreateDepth()
{
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physical_, VK_FORMAT_D32_SFLOAT, &properties);
    if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        throw std::runtime_error("D32 depth attachment is unsupported");
    depth_ = CreateImage(width_, height_, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT);
    depthReady_ = false;
}

void Sample::LoadTexture(const std::filesystem::path &path, uint32_t slot, VkFormat format)
{
    const auto image = LoadWicImage(path);
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physical_, format, &properties);
    const auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                          VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
        throw std::runtime_error("RGBA8 sampled texture is unsupported");
    textures_[slot] =
        CreateImage(image.width, image.height, format,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    staging_ = CreateBuffer(image.rgba.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, image.rgba.data());
    CheckVk(vkResetCommandBuffer(commands_, 0), "Texture command reset");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(commands_, &begin), "Texture command begin");
    ImageBarrier(commands_, textures_[slot].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {image.width, image.height, 1};
    vkCmdCopyBufferToImage(commands_, staging_.buffer, textures_[slot].image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    ImageBarrier(commands_, textures_[slot].image, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    CheckVk(vkEndCommandBuffer(commands_), "Texture command end");
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = commands_;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    CheckVk(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE), "Texture submit");
    CheckVk(vkQueueWaitIdle(queue_), "Texture upload wait");
    DestroyBuffer(staging_);
    if (!sampler_)
    {
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        const auto min = model_.sampler.minFilter;
        sampler.magFilter = model_.sampler.magFilter == 9728 ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        sampler.minFilter =
            (min == 9728 || min == 9984 || min == 9986) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        sampler.mipmapMode =
            (min == 9986 || min == 9987) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        auto address = [](uint32_t wrap)
        {
            return wrap == 33071   ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                   : wrap == 33648 ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
                                   : VK_SAMPLER_ADDRESS_MODE_REPEAT;
        };
        sampler.addressModeU = address(model_.sampler.wrapS);
        sampler.addressModeV = address(model_.sampler.wrapT);
        sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler.minLod = sampler.maxLod = 0; // Only mip 0 is uploaded.
        CheckVk(vkCreateSampler(device_, &sampler, nullptr, &sampler_), "vkCreateSampler");
    }
}

void Sample::CreatePipeline()
{
    if (!pipelineLayout_)
    {
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {6, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {7, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {8, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {9, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {11, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        };
        VkDescriptorSetLayoutCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptors.bindingCount = static_cast<uint32_t>(std::size(bindings));
        descriptors.pBindings = bindings;
        CheckVk(vkCreateDescriptorSetLayout(device_, &descriptors, nullptr, &descriptorLayout_),
                "Descriptor layout");
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &descriptorLayout_;
        CheckVk(vkCreatePipelineLayout(device_, &layout, nullptr, &pipelineLayout_),
                "vkCreatePipelineLayout");
    }
    const auto vertex = ReadBinary(AssetPath(L"Shaders/Vertex.spv"));
    const auto pixel = ReadBinary(AssetPath(L"Shaders/Pixel.spv"));
    auto shader = [&](const std::vector<uint8_t> &bytes, VkShaderModule &module)
    {
        if (bytes.size() % 4 || bytes.size() < 20)
            throw std::runtime_error("Invalid SPIR-V binary");
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = bytes.size();
        info.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
        CheckVk(vkCreateShaderModule(device_, &info, nullptr, &module), "vkCreateShaderModule");
    };
    shader(vertex, vertexShader_);
    shader(pixel, pixelShader_);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexShader_;
    stages[0].pName = "VSMain";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = pixelShader_;
    stages[1].pName = "PSMain";
    VkVertexInputBindingDescription binding{0, sizeof(PbrVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                                      {1, 0, VK_FORMAT_R32G32_SFLOAT, 12},
                                                      {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 20},
                                                      {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32}};
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
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState attachment{};
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
    rendering.pColorAttachmentFormats = &hdrFormat_;
    rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
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
    info.layout = pipelineLayout_;
    CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_),
            "vkCreateGraphicsPipelines");

    vkDestroyShaderModule(device_, pixelShader_, nullptr);
    pixelShader_ = VK_NULL_HANDLE;
    shader(ReadBinary(AssetPath(L"Shaders/MonitorPixel.spv")), pixelShader_);
    stages[1].module = pixelShader_;
    CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &monitorPipeline_),
            "Unlit monitor pipeline");
    vkDestroyShaderModule(device_, vertexShader_, nullptr);
    vertexShader_ = VK_NULL_HANDLE;
    vkDestroyShaderModule(device_, pixelShader_, nullptr);
    pixelShader_ = VK_NULL_HANDLE;

    if constexpr (POST_TECHNIQUE >= 16)
    {
        shader(ReadBinary(AssetPath(L"Shaders/ParticleVertex.spv")), vertexShader_);
        shader(ReadBinary(AssetPath(L"Shaders/ParticlePixel.spv")), pixelShader_);
        stages[0].module = vertexShader_;
        stages[1].module = pixelShader_;
        VkVertexInputBindingDescription particleBinding{0, sizeof(Particle), VK_VERTEX_INPUT_RATE_INSTANCE};
        VkVertexInputAttributeDescription attrs[] = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                                                     {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32}};
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &particleBinding;
        input.vertexAttributeDescriptionCount = 2;
        input.pVertexAttributeDescriptions = attrs;
        info.pVertexInputState = &input;
        depth.depthWriteEnable = VK_FALSE;
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = attachment.dstColorBlendFactor = attachment.srcAlphaBlendFactor =
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &particlePipeline_),
                "Particle billboard pipeline");
        vkDestroyShaderModule(device_, vertexShader_, nullptr);
        vertexShader_ = VK_NULL_HANDLE;
        vkDestroyShaderModule(device_, pixelShader_, nullptr);
        pixelShader_ = VK_NULL_HANDLE;
        attachment.blendEnable = VK_FALSE;
        info.pVertexInputState = &vertexInput;
    }
    const auto skyV = ReadBinary(AssetPath(L"Shaders/SkyVertex.spv")),
               skyP = ReadBinary(AssetPath(L"Shaders/SkyPixel.spv"));
    shader(skyV, vertexShader_);
    shader(skyP, pixelShader_);
    stages[0].module = vertexShader_;
    stages[1].module = pixelShader_;
    auto skyInput = vertexInput;
    skyInput.vertexBindingDescriptionCount = skyInput.vertexAttributeDescriptionCount = 0;
    info.pVertexInputState = &skyInput;
    depth.depthWriteEnable = VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &skyPipeline_),
            "Sky pipeline");
    vkDestroyShaderModule(device_, vertexShader_, nullptr);
    vertexShader_ = VK_NULL_HANDLE;
    vkDestroyShaderModule(device_, pixelShader_, nullptr);
    pixelShader_ = VK_NULL_HANDLE;
    if constexpr (LAB_TECHNIQUE == 13)
    {
        auto v = ReadBinary(AssetPath(L"Shaders/ShadowVertex.spv"));
        shader(v, vertexShader_);
        stages[0].module = vertexShader_;
        info.stageCount = 1;
        info.pVertexInputState = &vertexInput;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        rendering.colorAttachmentCount = 0;
        rendering.pColorAttachmentFormats = nullptr;
        blend.attachmentCount = 0;
        CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &shadowPipeline_),
                "Shadow pipeline");
        vkDestroyShaderModule(device_, vertexShader_, nullptr);
        vertexShader_ = VK_NULL_HANDLE;
    }
}
void Sample::CreateScene(const Options &)
{
    objects_.resize(scene_.objects.size());
    textures_.resize(objects_.size() * 9);
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_, &props);
    const auto a = props.limits.minUniformBufferOffsetAlignment;
    constantStride_ = (sizeof(LabConstants) + a - 1) / a * a;
    constants_ = CreateBuffer(constantStride_ * objects_.size() * 2, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    for (size_t i = 0; i < objects_.size(); ++i)
    {
        auto &o = objects_[i];
        const auto &m = scene_.objects[i].model;
        auto v = MakePbrVertices(m);
        o.vertices = CreateBuffer(v.size() * sizeof(PbrVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, v.data());
        o.indices =
            CreateBuffer(m.mesh.indices.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, m.mesh.indices.data());
        o.count = static_cast<uint32_t>(m.mesh.indices.size());
        LoadTexture(m.albedoPath, static_cast<uint32_t>(i * 9), VK_FORMAT_R8G8B8A8_SRGB);
        LoadTexture(m.normalPath, static_cast<uint32_t>(i * 9 + 1), VK_FORMAT_R8G8B8A8_UNORM);
        LoadTexture(m.ormPath, static_cast<uint32_t>(i * 9 + 2), VK_FORMAT_R8G8B8A8_UNORM);
        LoadTexture(m.emissivePath, static_cast<uint32_t>(i * 9 + 3), VK_FORMAT_R8G8B8A8_SRGB);
    }
    for (size_t e = 0; e < 2; ++e)
        for (size_t kind = 0; kind < 3; ++kind)
        {
            const wchar_t *suffix = kind == 0   ? L"_Environment.dds"
                                    : kind == 1 ? L"_Irradiance.dds"
                                                : L"_Specular.dds";
            environments_[e][kind] =
                LoadDdsTexture(std::filesystem::path(scene_.environments[e].wstring() + suffix));
        }
    environments_[2][1] = environments_[0][1];
    environments_[2][2] = environments_[0][2];
    environments_[2][0] = LoadDdsTexture(AssetPath(L"Assets/Environments/LightingLab/Orientation.dds"));
    brdf_ = LoadDdsTexture(AssetPath(L"Assets/Environments/LightingLab/BRDF.dds"));
    shadow_ = CreateImage(2048, 2048, VK_FORMAT_D32_SFLOAT,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_IMAGE_ASPECT_DEPTH_BIT);
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.maxLod = 8;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    CheckVk(vkCreateSampler(device_, &sampler, nullptr, &environmentSampler_), "Environment sampler");
    const uint32_t n = static_cast<uint32_t>(objects_.size());
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, n},
                                    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, n * 9},
                                    {VK_DESCRIPTOR_TYPE_SAMPLER, n * 2}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = n;
    pool.poolSizeCount = 3;
    pool.pPoolSizes = sizes;
    CheckVk(vkCreateDescriptorPool(device_, &pool, nullptr, &descriptorPool_), "Lab descriptor pool");
    std::vector<VkDescriptorSetLayout> layouts(n, descriptorLayout_);
    descriptorSets_.resize(n);
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = descriptorPool_;
    alloc.descriptorSetCount = n;
    alloc.pSetLayouts = layouts.data();
    CheckVk(vkAllocateDescriptorSets(device_, &alloc, descriptorSets_.data()), "Lab descriptor sets");
    UpdateLabDescriptors();
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
    b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
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
void Sample::UpdateLabDescriptors()
{
    for (size_t i = 0; i < objects_.size(); ++i)
    {
        VkDescriptorBufferInfo buffer{constants_.buffer, 0, sizeof(LabConstants)};
        VkDescriptorImageInfo images[11]{};
        for (size_t t = 0; t < 4; ++t)
            images[t] = {VK_NULL_HANDLE, textures_[i * 9 + t].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        for (size_t t = 0; t < 3; ++t)
            images[4 + t] = {VK_NULL_HANDLE, environments_[panel_.environmentIndex][t].view,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        images[7] = {VK_NULL_HANDLE, brdf_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        images[8] = {VK_NULL_HANDLE, shadow_.view, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL};
        images[9] = {sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        images[10] = {environmentSampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        VkWriteDescriptorSet writes[12]{};
        for (auto &w : writes)
        {
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = descriptorSets_[i];
            w.descriptorCount = 1;
        }
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[0].pBufferInfo = &buffer;
        const uint32_t binding[] = {1, 3, 4, 5, 6, 7, 8, 9, 10, 2, 11};
        for (size_t t = 0; t < 11; ++t)
        {
            writes[t + 1].dstBinding = binding[t];
            writes[t + 1].descriptorType =
                t < 9 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLER;
            writes[t + 1].pImageInfo = &images[t];
        }
        vkUpdateDescriptorSets(device_, 12, writes, 0, nullptr);
    }

    for (size_t i = 0; i < objects_.size(); ++i)
        if (scene_.objects[i].renderTexture)
        {
            VkDescriptorImageInfo image{VK_NULL_HANDLE, postImages_[1].view,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = descriptorSets_[i];
            w.dstBinding = 1;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            w.pImageInfo = &image;
            vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
        }
}

void Sample::CreatePostTargets()
{
    for (auto &image : postImages_)
        DestroyImage(image);
    DestroyImage(monitorDepth_);
    postLayouts_.fill(VK_IMAGE_LAYOUT_UNDEFINED);
    monitorDepthReady_ = false;
    VkFormatProperties supported{};
    vkGetPhysicalDeviceFormatProperties(physical_, hdrFormat_, &supported);
    const auto required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                          (POST_TECHNIQUE >= 15 ? VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT : 0);
    if ((supported.optimalTilingFeatures & required) != required)
        throw std::runtime_error("RGBA16F render/storage support required");
    for (uint32_t i = 0; i < (POST_TECHNIQUE >= 15 ? 5u : 2u); ++i)
    {
        uint32_t w = i == 1   ? 512
                     : i >= 2 ? (width_ + 1) / 2
                              : width_,
                 h = i == 1   ? 512
                     : i >= 2 ? (height_ + 1) / 2
                              : height_;
        postImages_[i] =
            CreateImage(w, h, hdrFormat_,
                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                            (i < 2 ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_STORAGE_BIT),
                        VK_IMAGE_ASPECT_COLOR_BIT);
    }
    monitorDepth_ = CreateImage(512, 512, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                VK_IMAGE_ASPECT_DEPTH_BIT);
    if (postPool_)
        UpdatePostDescriptors();
}
void Sample::CreatePostResources()
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_, &props);
    auto align = props.limits.minUniformBufferOffsetAlignment;
    constantStride_ = (sizeof(LabConstants) + align - 1) / align * align;
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    CheckVk(vkCreateSampler(device_, &sampler, nullptr, &postSampler_), "Post sampler");
    CreatePostTargets();
    postConstants_ = CreateBuffer(5 * constantStride_, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    if constexpr (POST_TECHNIQUE >= 16)
    {
        VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        b.size = 65536 * sizeof(Particle);
        b.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        CheckVk(vkCreateBuffer(device_, &b, nullptr, &particles_.buffer), "Device-local particle buffer");
        VkMemoryRequirements r{};
        vkGetBufferMemoryRequirements(device_, particles_.buffer, &r);
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        a.allocationSize = r.size;
        a.memoryTypeIndex = MemoryType(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CheckVk(vkAllocateMemory(device_, &a, nullptr, &particles_.memory), "Particle device memory");
        CheckVk(vkBindBufferMemory(device_, particles_.buffer, particles_.memory, 0), "Particle bind");
        Trace("Particles: same storage / instance vertex buffer, device-local; 48-byte stride");
    }
    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
         nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = 5;
    info.pBindings = bindings;
    CheckVk(vkCreateDescriptorSetLayout(device_, &info, nullptr, &postDescriptorLayout_), "Post descriptors");
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &postDescriptorLayout_;
    CheckVk(vkCreatePipelineLayout(device_, &layout, nullptr, &postLayout_), "Post pipeline layout");
    if constexpr (POST_TECHNIQUE >= 16)
    {
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        CheckVk(vkCreateDescriptorSetLayout(device_, &info, nullptr, &particleDescriptorLayout_),
                "Particle descriptors");
        layout.pSetLayouts = &particleDescriptorLayout_;
        CheckVk(vkCreatePipelineLayout(device_, &layout, nullptr, &particleLayout_),
                "Particle compute layout");
    }
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 5},
                                    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10},
                                    {VK_DESCRIPTOR_TYPE_SAMPLER, 5},
                                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4},
                                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 5;
    pool.poolSizeCount = 5;
    pool.pPoolSizes = sizes;
    CheckVk(vkCreateDescriptorPool(device_, &pool, nullptr, &postPool_), "Post pool");
    VkDescriptorSetLayout layouts[] = {
        postDescriptorLayout_, postDescriptorLayout_, postDescriptorLayout_, postDescriptorLayout_,
        POST_TECHNIQUE >= 16 ? particleDescriptorLayout_ : postDescriptorLayout_};
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = postPool_;
    alloc.descriptorSetCount = POST_TECHNIQUE == 14 ? 1 : POST_TECHNIQUE == 15 ? 4 : 5;
    alloc.pSetLayouts = layouts;
    CheckVk(vkAllocateDescriptorSets(device_, &alloc, postSets_.data()), "Post sets");
    UpdatePostDescriptors();
    CreatePostPipelines();
}
void Sample::UpdatePostDescriptors()
{
    for (uint32_t pass = 0; pass < (POST_TECHNIQUE == 14 ? 1u : POST_TECHNIQUE == 15 ? 4u : 5u); ++pass)
    {
        const uint32_t selected = panel_.postView == 2   ? 2
                                  : panel_.postView == 3 ? 3
                                  : panel_.postView == 4 ? 4
                                  : panel_.postView == 5 ? 1
                                                         : 0;
        uint32_t source = pass == 0 ? selected : pass == 1 ? 0 : pass == 2 ? 2 : pass == 3 ? 3 : 0;
        VkDescriptorBufferInfo uniform{postConstants_.buffer, pass * constantStride_, sizeof(PostConstants)},
            particle{particles_.buffer, 0, 65536 * sizeof(Particle)};
        VkDescriptorImageInfo input{VK_NULL_HANDLE, postImages_[source].view,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo bloom{VK_NULL_HANDLE, postImages_[POST_TECHNIQUE >= 15 ? 4 : 0].view,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo sampler{postSampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        VkDescriptorImageInfo output{VK_NULL_HANDLE,
                                     postImages_[pass == 2              ? 3
                                                 : pass == 3            ? 4
                                                 : POST_TECHNIQUE >= 15 ? 2
                                                                        : 0]
                                         .view,
                                     VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet writes[5]{};
        const VkDescriptorType types[] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                          VK_DESCRIPTOR_TYPE_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                          pass == 4 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                                    : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
        for (uint32_t i = 0; i < 5; ++i)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = postSets_[pass];
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = types[i];
        }
        writes[0].pBufferInfo = &uniform;
        writes[1].pImageInfo = &input;
        writes[2].pImageInfo = &sampler;
        writes[3].pImageInfo = &bloom;
        if (pass == 4)
            writes[4].pBufferInfo = &particle;
        else
            writes[4].pImageInfo = &output;
        vkUpdateDescriptorSets(device_, POST_TECHNIQUE == 14 ? 4 : 5, writes, 0, nullptr);
    }
}
void Sample::DestroyPostPipelines() noexcept
{
    for (auto &p : bloomPipelines_)
    {
        if (p)
            vkDestroyPipeline(device_, p, nullptr);
        p = VK_NULL_HANDLE;
    }
    for (auto *p : {&postPipeline_, &testPipeline_, &particleCompute_})
    {
        if (*p)
            vkDestroyPipeline(device_, *p, nullptr);
        *p = VK_NULL_HANDLE;
    }
    for (auto module : postModules_)
        vkDestroyShaderModule(device_, module, nullptr);
    postModules_.clear();
}
void Sample::CreatePostPipelines()
{
    auto module = [&](const wchar_t *name)
    {
        auto bytes = ReadBinary(AssetPath(std::filesystem::path(L"Shaders") / name));
        VkShaderModuleCreateInfo i{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        i.codeSize = bytes.size();
        i.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
        VkShaderModule m = VK_NULL_HANDLE;
        CheckVk(vkCreateShaderModule(device_, &i, nullptr, &m), "Post shader module");
        postModules_.push_back(m);
        return m;
    };
    auto v = module(L"PostVertex.spv"), p = module(L"PostPixel.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto &s : stages)
    {
        s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        s.pName = "VSMain";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = v;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = p;
    stages[1].pName = "PSMain";
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    VkDynamicState ds[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = ds;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat_;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = postLayout_;
    CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &postPipeline_),
            "Post graphics pipeline");
    p = module(L"TestPixel.spv");
    stages[1].module = p;
    rendering.pColorAttachmentFormats = &hdrFormat_;
    rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
    CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &testPipeline_),
            "Test HDR pipeline");
    if constexpr (POST_TECHNIQUE >= 15)
        for (uint32_t i = 0; i < 3; ++i)
        {
            auto cs = module(i == 0 ? L"Extract.spv" : L"Blur.spv");
            VkComputePipelineCreateInfo c{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            c.layout = postLayout_;
            c.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                       nullptr,
                       0,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       cs,
                       "CSMain",
                       nullptr};
            CheckVk(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &c, nullptr, &bloomPipelines_[i]),
                    "Bloom compute pipeline");
        }
    if constexpr (POST_TECHNIQUE >= 16)
    {
        auto cs = module(L"ParticleUpdate.spv");
        VkComputePipelineCreateInfo c{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        c.layout = particleLayout_;
        c.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                   nullptr,
                   0,
                   VK_SHADER_STAGE_COMPUTE_BIT,
                   cs,
                   "CSMain",
                   nullptr};
        CheckVk(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &c, nullptr, &particleCompute_),
                "Particle compute pipeline");
    }
}
void Sample::PostTransition(uint32_t i, VkImageLayout target)
{
    auto old = postLayouts_[i];
    auto stage = [](VkImageLayout l) -> VkPipelineStageFlags2
    {
        return l == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_NONE
               : l == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                   ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
               : l == VK_IMAGE_LAYOUT_GENERAL ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
               : l == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                   ? VK_PIPELINE_STAGE_2_COPY_BIT
                   : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    };
    auto access = [](VkImageLayout l) -> VkAccessFlags2
    {
        return l == VK_IMAGE_LAYOUT_UNDEFINED                  ? 0
               : l == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
               : l == VK_IMAGE_LAYOUT_GENERAL                  ? VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
               : l == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL     ? VK_ACCESS_2_TRANSFER_READ_BIT
                                                               : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    };
    ImageBarrier(commands_, postImages_[i].image, VK_IMAGE_ASPECT_COLOR_BIT, old, target, stage(old),
                 access(old), stage(target), access(target));
    postLayouts_[i] = target;
}
void Sample::DrawHdrScene(uint32_t target, uint32_t bank)
{
    PostTransition(target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    auto &depthImage = bank ? monitorDepth_ : depth_;
    bool &ready = bank ? monitorDepthReady_ : depthReady_;
    ImageBarrier(
        commands_, depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT,
        ready ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        ready ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
              : VK_PIPELINE_STAGE_2_NONE,
        ready ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    ready = true;
    uint32_t w = bank ? 512 : width_, h = bank ? 512 : height_, left = bank ? 0 : panel_.SceneLeft(width_);
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = postImages_[target].view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0, 0, 0, 1}};
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = depthImage.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {1, 0};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = {w, h};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(commands_, &rendering);
    VkViewport viewport{float(left), float(h), float(w - left), -float(h), 0, 1};
    VkRect2D scissor{{int32_t(left), 0}, {w - left, h}};
    vkCmdSetViewport(commands_, 0, 1, &viewport);
    vkCmdSetScissor(commands_, 0, 1, &scissor);
    uint32_t offset = uint32_t(bank * objects_.size() * constantStride_);
    vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                            &descriptorSets_[0], 1, &offset);
    vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
    vkCmdDraw(commands_, 3, 1, 0, 0);
    for (size_t i = 0; i < objects_.size(); ++i)
    {
        if (bank && scene_.objects[i].renderTexture)
            continue;
        auto &o = objects_[i];
        offset = uint32_t((bank * objects_.size() + i) * constantStride_);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          scene_.objects[i].renderTexture ? monitorPipeline_ : pipeline_);
        vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                                &descriptorSets_[i], 1, &offset);
        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(commands_, 0, 1, &o.vertices.buffer, &zero);
        vkCmdBindIndexBuffer(commands_, o.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commands_, o.count, 1, 0, 0, 0);
    }
    if constexpr (POST_TECHNIQUE >= 16)
    {
        offset = uint32_t(bank * objects_.size() * constantStride_);
        vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                                &descriptorSets_[0], 1, &offset);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipeline_);
        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(commands_, 0, 1, &particles_.buffer, &zero);
        vkCmdDraw(commands_, 6, uint32_t(panel_.particleCount), 0, 0);
    }
    if (!bank && panel_.testImage)
    {
        viewport = {0, float(height_), float(width_), -float(height_), 0, 1};
        scissor = {{0, 0}, {width_, height_}};
        vkCmdSetViewport(commands_, 0, 1, &viewport);
        vkCmdSetScissor(commands_, 0, 1, &scissor);
        vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, testPipeline_);
        vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout_, 0, 1, &postSets_[0],
                                0, nullptr);
        vkCmdDraw(commands_, 3, 1, 0, 0);
    }
    vkCmdEndRendering(commands_);
    PostTransition(target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
void Sample::DestroyPostResources() noexcept
{
    if (postSampler_)
        vkDestroySampler(device_, postSampler_, nullptr);
    postSampler_ = VK_NULL_HANDLE;
    DestroyPostPipelines();
    for (auto &image : postImages_)
        DestroyImage(image);
    DestroyImage(monitorDepth_);
    DestroyBuffer(postConstants_);
    DestroyBuffer(particles_);
    for (auto *p : {&monitorPipeline_, &particlePipeline_})
    {
        if (*p)
            vkDestroyPipeline(device_, *p, nullptr);
        *p = VK_NULL_HANDLE;
    }
    if (postPool_)
        vkDestroyDescriptorPool(device_, postPool_, nullptr);
    postPool_ = VK_NULL_HANDLE;
    if (postLayout_)
        vkDestroyPipelineLayout(device_, postLayout_, nullptr);
    postLayout_ = VK_NULL_HANDLE;
    if (particleLayout_)
        vkDestroyPipelineLayout(device_, particleLayout_, nullptr);
    particleLayout_ = VK_NULL_HANDLE;
    if (postDescriptorLayout_)
        vkDestroyDescriptorSetLayout(device_, postDescriptorLayout_, nullptr);
    postDescriptorLayout_ = VK_NULL_HANDLE;
    if (particleDescriptorLayout_)
        vkDestroyDescriptorSetLayout(device_, particleDescriptorLayout_, nullptr);
    particleDescriptorLayout_ = VK_NULL_HANDLE;
}

void Sample::DumpGpuData(const std::filesystem::path &path)
{
    CheckVk(vkDeviceWaitIdle(device_), "GPU capture wait");
    auto dir = path.parent_path() / (path.stem().wstring() + L".gpu");
    std::filesystem::create_directories(dir);
    CheckVk(vkResetCommandBuffer(commands_, 0), "Capture command reset");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CheckVk(vkBeginCommandBuffer(commands_, &begin), "Capture begin");
    const uint32_t count = POST_TECHNIQUE >= 15 ? 5 : 2;
    std::array<Buffer, 6> reads{};
    std::array<uint32_t, 5> widths{}, heights{};
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t w = i == 1   ? 512
                     : i >= 2 ? (width_ + 1) / 2
                              : width_,
                 h = i == 1   ? 512
                     : i >= 2 ? (height_ + 1) / 2
                              : height_;
        widths[i] = w;
        heights[i] = h;
        reads[i] = CreateBuffer(VkDeviceSize(w) * h * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        PostTransition(i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(commands_, postImages_[i].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               reads[i].buffer, 1, &copy);
        PostTransition(i, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    if constexpr (POST_TECHNIQUE >= 16)
    {
        reads[5] = CreateBuffer(65536 * sizeof(Particle), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        b.buffer = particles_.buffer;
        b.size = VK_WHOLE_SIZE;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        b.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &b;
        vkCmdPipelineBarrier2(commands_, &dep);
        VkBufferCopy copy{0, 0, 65536 * sizeof(Particle)};
        vkCmdCopyBuffer(commands_, particles_.buffer, reads[5].buffer, 1, &copy);
        b.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        b.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        b.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        vkCmdPipelineBarrier2(commands_, &dep);
    }
    std::vector<VkBufferMemoryBarrier2> barriers;
    for (uint32_t i = 0; i < (POST_TECHNIQUE >= 16 ? 6 : count); ++i)
    {
        VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        b.buffer = reads[i].buffer;
        b.size = VK_WHOLE_SIZE;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        b.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        barriers.push_back(b);
    }
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = uint32_t(barriers.size());
    dependency.pBufferMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(commands_, &dependency);
    CheckVk(vkEndCommandBuffer(commands_), "Capture end");
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = commands_;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    CheckVk(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE), "Capture submit");
    CheckVk(vkQueueWaitIdle(queue_), "Capture completion");
    auto invalidate = [&](Buffer &b)
    {
        if (!b.coherent)
        {
            VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            r.memory = b.memory;
            r.size = VK_WHOLE_SIZE;
            CheckVk(vkInvalidateMappedMemoryRanges(device_, 1, &r), "Capture invalidate");
        }
    };
    nlohmann::json metadata;
    metadata["api"] = "Vulkan";
    metadata["images"] = nlohmann::json::array();
    const char *names[] = {"scene", "monitor", "extract", "horizontal", "bloom"};
    for (uint32_t i = 0; i < count; ++i)
    {
        invalidate(reads[i]);
        std::vector<uint8_t> bytes(size_t(widths[i]) * heights[i] * 8);
        std::memcpy(bytes.data(), reads[i].mapped, bytes.size());
        metadata["images"].push_back(CaptureHalfImage(dir, names[i], widths[i], heights[i], bytes));
        DestroyBuffer(reads[i]);
    }
    if constexpr (POST_TECHNIQUE >= 16)
    {
        invalidate(reads[5]);
        CaptureParticles(dir, reads[5].mapped, uint32_t(panel_.particleCount), simulationSteps_, metadata);
        DestroyBuffer(reads[5]);
    }
    CaptureMetadata(dir, metadata);
    Trace("GPU capture: finite HDR and device-local data verified");
}
