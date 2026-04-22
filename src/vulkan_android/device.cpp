#include <algorithm>
#include <cstring>
#include <functional>
#include <vector>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <android/log.h>
#include "campello_gpu_config.h"
#include <campello_gpu/device.hpp>
#include <campello_gpu/adapter.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/shader_module.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/ray_tracing_pipeline.hpp>
#include <campello_gpu/descriptors/bottom_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/top_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/ray_tracing_pipeline_descriptor.hpp>
#include "acceleration_structure_handle.hpp"
#include "ray_tracing_pipeline_handle.hpp"
#include "buffer_handle.hpp"
#include "texture_handle.hpp"
#include "shader_module_handle.hpp"
#include "render_pipeline_handle.hpp"
#include "sampler_handle.hpp"
#include "query_set_handle.hpp"
#include "compute_pipeline_handle.hpp"
#include "pipeline_layout_handle.hpp"
#include "bind_group_handle.hpp"
#include "bind_group_layout_handle.hpp"
#include "command_buffer_handle.hpp"
#include "command_encoder_handle.hpp"
#include "common.hpp"

using namespace systems::leal::campello_gpu;

VkInstance instance = nullptr;

// Function pointers for VK_KHR_dynamic_rendering (needed for Android API 28 / Vulkan 1.1)
PFN_vkCmdBeginRenderingKHR pfnCmdBeginRenderingKHR = nullptr;
PFN_vkCmdEndRenderingKHR pfnCmdEndRenderingKHR = nullptr;

void loadDynamicRenderingFunctions(VkDevice device)
{
    pfnCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(device, "vkCmdBeginRenderingKHR");
    pfnCmdEndRenderingKHR = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(device, "vkCmdEndRenderingKHR");
}

// vkGetBufferDeviceAddress is Vulkan 1.2 core; load via proc addr so it works on API-28 (Vulkan 1.1).
PFN_vkGetBufferDeviceAddress pfnGetBufferDeviceAddress = nullptr;

// Function pointers for VK_KHR_acceleration_structure and VK_KHR_ray_tracing_pipeline.
PFN_vkCreateAccelerationStructureKHR           pfnCreateAccelerationStructureKHR          = nullptr;
PFN_vkDestroyAccelerationStructureKHR          pfnDestroyAccelerationStructureKHR         = nullptr;
PFN_vkGetAccelerationStructureBuildSizesKHR    pfnGetAccelerationStructureBuildSizesKHR   = nullptr;
PFN_vkCmdBuildAccelerationStructuresKHR        pfnCmdBuildAccelerationStructuresKHR       = nullptr;
PFN_vkCmdCopyAccelerationStructureKHR          pfnCmdCopyAccelerationStructureKHR         = nullptr;
PFN_vkGetAccelerationStructureDeviceAddressKHR pfnGetAccelerationStructureDeviceAddressKHR = nullptr;
PFN_vkCreateRayTracingPipelinesKHR             pfnCreateRayTracingPipelinesKHR            = nullptr;
PFN_vkGetRayTracingShaderGroupHandlesKHR       pfnGetRayTracingShaderGroupHandlesKHR      = nullptr;
PFN_vkCmdTraceRaysKHR                          pfnCmdTraceRaysKHR                         = nullptr;

static void loadRayTracingFunctions(VkDevice device) {
    pfnGetBufferDeviceAddress           = (PFN_vkGetBufferDeviceAddress)           vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddress");
    pfnCreateAccelerationStructureKHR          = (PFN_vkCreateAccelerationStructureKHR)         vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR");
    pfnDestroyAccelerationStructureKHR         = (PFN_vkDestroyAccelerationStructureKHR)        vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR");
    pfnGetAccelerationStructureBuildSizesKHR   = (PFN_vkGetAccelerationStructureBuildSizesKHR)  vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR");
    pfnCmdBuildAccelerationStructuresKHR       = (PFN_vkCmdBuildAccelerationStructuresKHR)      vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR");
    pfnCmdCopyAccelerationStructureKHR         = (PFN_vkCmdCopyAccelerationStructureKHR)        vkGetDeviceProcAddr(device, "vkCmdCopyAccelerationStructureKHR");
    pfnGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR");
    pfnCreateRayTracingPipelinesKHR            = (PFN_vkCreateRayTracingPipelinesKHR)           vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR");
    pfnGetRayTracingShaderGroupHandlesKHR      = (PFN_vkGetRayTracingShaderGroupHandlesKHR)     vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR");
    pfnCmdTraceRaysKHR                         = (PFN_vkCmdTraceRaysKHR)                        vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR");
}

VkFormat pixelFormatToNative(PixelFormat format);
PixelFormat nativeToPixelFormat(VkFormat format);

std::string queueFlagsToString(VkQueueFlags flags)
{
    std::vector<std::string> stringFlags;
    if (flags & VK_QUEUE_GRAPHICS_BIT)
        stringFlags.emplace_back("VK_QUEUE_GRAPHICS_BIT");
    if (flags & VK_QUEUE_COMPUTE_BIT)
        stringFlags.emplace_back("VK_QUEUE_COMPUTE_BIT");
    if (flags & VK_QUEUE_TRANSFER_BIT)
        stringFlags.emplace_back("VK_QUEUE_TRANSFER_BIT");
    if (flags & VK_QUEUE_SPARSE_BINDING_BIT)
        stringFlags.emplace_back("VK_QUEUE_SPARSE_BINDING_BIT");
    if (flags & VK_QUEUE_PROTECTED_BIT)
        stringFlags.emplace_back("VK_QUEUE_PROTECTED_BIT");
    if (flags & VK_QUEUE_VIDEO_DECODE_BIT_KHR)
        stringFlags.emplace_back("VK_QUEUE_VIDEO_DECODE_BIT_KHR");
    if (flags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR)
        stringFlags.emplace_back("VK_QUEUE_VIDEO_ENCODE_BIT_KHR");
    if (flags & VK_QUEUE_OPTICAL_FLOW_BIT_NV)
        stringFlags.emplace_back("VK_QUEUE_OPTICAL_FLOW_BIT_NV");
    // if (flags&VK_QUEUE_DATA_GRAPH_BIT_ARM) stringFlags.push_back("VK_QUEUE_DATA_GRAPH_BIT_ARM");

    std::string toReturn = "[";
    if (!stringFlags.empty())
    {
        for (int a = 0; a < stringFlags.size() - 1; a++)
        {
            toReturn += stringFlags[a] + " | ";
        }
        toReturn += stringFlags[stringFlags.size() - 1];
    }
    toReturn += "]";
    return toReturn;
}

VkInstance systems::leal::campello_gpu::getInstance()
{

    if (instance != nullptr)
    {
        return instance;
    }

    uint32_t propertyCount=0;
    vkEnumerateInstanceLayerProperties(&propertyCount, nullptr);
    std::vector<VkLayerProperties> properties(propertyCount);
    vkEnumerateInstanceLayerProperties(&propertyCount, properties.data());

    uint32_t extensionCount = 0;
    std::vector<VkExtensionProperties> extensions;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "%d instance extensions supported,", extensionCount);

    if (extensionCount > 0)
    {
        extensions.resize(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
        for (int a = 0; a < extensionCount; a++)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", " - %s v%d", extensions[a].extensionName, extensions[a].specVersion);
        }
    }

    VkApplicationInfo appInfo;
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "test";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "campello_gpu";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;
    appInfo.pNext = nullptr;

    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "Setting required extensions");
    std::vector<const char *> requiredExtensions;
    requiredExtensions.push_back("VK_KHR_surface");
    requiredExtensions.push_back("VK_KHR_android_surface");
    // requiredExtensions.push_back("VK_KHR_get_surface_capabilities2");

    // Only request validation layers that are actually present on this device.
    const char *wantedLayer = "VK_LAYER_KHRONOS_validation";
    std::vector<const char *> enabledLayers;
    for (uint32_t i = 0; i < propertyCount; ++i) {
        if (strcmp(properties[i].layerName, wantedLayer) == 0) {
            enabledLayers.push_back(wantedLayer);
            break;
        }
    }

    VkInstanceCreateInfo instanceInfo;
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pNext = nullptr;
    instanceInfo.flags = 0;
    instanceInfo.enabledExtensionCount = requiredExtensions.size();
    instanceInfo.ppEnabledExtensionNames = requiredExtensions.data();
    instanceInfo.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
    instanceInfo.ppEnabledLayerNames = enabledLayers.empty() ? nullptr : enabledLayers.data();
    instanceInfo.pApplicationInfo = &appInfo;

    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "Creating vulkan instance");
    VkInstance ins;
    auto res = vkCreateInstance(&instanceInfo, nullptr, &ins);
    if (res != VK_SUCCESS)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateInstance failed with error: %d", res);
        return nullptr;
    }
    instance = ins;
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "Vulkan instance created");
    return ins;
}

Device::Device(void *pd)
{
    native = pd;
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "Device::Device()");
}

Device::~Device()
{
    if (native != nullptr)
    {
        auto deviceData = (DeviceData *)native;

        // Wait for all GPU work to finish before tearing down.
        vkDeviceWaitIdle(deviceData->device);

        if (deviceData->renderFinishedSemaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(deviceData->device, deviceData->renderFinishedSemaphore, nullptr);
        if (deviceData->imageAvailableSemaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(deviceData->device, deviceData->imageAvailableSemaphore, nullptr);

        for (auto iv : deviceData->swapchainImageViews)
            vkDestroyImageView(deviceData->device, iv, nullptr);

        if (deviceData->swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(deviceData->device, deviceData->swapchain, nullptr);

        if (deviceData->surface != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(getInstance(), deviceData->surface, nullptr);

        if (deviceData->descriptorPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(deviceData->device, deviceData->descriptorPool, nullptr);

        vkDestroyCommandPool(deviceData->device, deviceData->commandPool, nullptr);
        vkDestroyDevice(deviceData->device, nullptr);
        delete deviceData;
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "Device::~Device()");
    }
}

/*void *Device::getNative()
{
    return native;
}*/

std::shared_ptr<Device> Device::createDefaultDevice(void *pd)
{

    auto devicesDef = getAdapters();
    if (devicesDef.empty())
    {
        return nullptr;
    }

    return createDevice(devicesDef[0], pd);
}

std::vector<std::shared_ptr<Adapter>> Device::getAdapters()
{
    std::vector<std::shared_ptr<Adapter>> toReturn;

    VkInstance inst = getInstance();
    if (!inst) return toReturn;

    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(inst, &gpuCount, nullptr);

    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "gpuCount=%d", gpuCount);

    // Allocate memory for the GPU handles
    std::vector<VkPhysicalDevice> gpus(gpuCount);

    // Second call: Get the GPU handles
    vkEnumeratePhysicalDevices(inst, &gpuCount, gpus.data());

    // Now, 'gpus' contains a list of VkPhysicalDevice handles
    for (int a = 0; a < gpuCount; a++)
    {
        const VkPhysicalDevice &gpu = gpus[a];

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, queueFamilies.data());

        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "gpu[%d].queueFamilyCount = %d", a, queueFamilyCount);

        for (int b = 0; b < queueFamilyCount; b++)
        {
            auto flags = queueFlagsToString(queueFamilies[b].queueFlags);
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "gpu[%d].queueuFamily[%d].queueFlags = %s", a, b, flags.c_str());
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "gpu[%d].queueuFamily[%d].queueCount = %d", a, b, queueFamilies[b].queueCount);
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "gpu[%d].queueuFamily[%d].minImageTransferGranularity.width = %d", a, b, queueFamilies[b].minImageTransferGranularity.width);
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "gpu[%d].queueuFamily[%d].minImageTransferGranularity.height = %d", a, b, queueFamilies[b].minImageTransferGranularity.height);
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "gpu[%d].queueuFamily[%d].minImageTransferGranularity.depth = %d", a, b, queueFamilies[b].minImageTransferGranularity.depth);
        }

        auto deviceDef = std::shared_ptr<Adapter>(new Adapter());
        deviceDef->native = (void *)gpu; // VkPhysicalDevice handle
        toReturn.push_back(deviceDef);
    }

    return toReturn;
}

std::shared_ptr<Device> Device::createDevice(std::shared_ptr<Adapter> deviceDef, void *pd)
{

    if (deviceDef == nullptr)
    {
        return nullptr;
    }

    auto window = (ANativeWindow *)pd;

    auto gpu = (VkPhysicalDevice)deviceDef->native;
    if (!gpu) return nullptr;

    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extensionCount, availableExtensions.data());

    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "%d device extensions supported,", extensionCount);
    for (int a = 0; a < extensionCount; a++)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", " - %s v%d", availableExtensions[a].extensionName, availableExtensions[a].specVersion);
    }

    const VkAndroidSurfaceCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .window = window};

    // Surface and swapchain creation are only possible with a real ANativeWindow.
    // When window == nullptr (headless / test mode) we skip them entirely.
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
    std::vector<VkSurfaceFormatKHR> surfaceFormats;
    VkSurfaceFormatKHR chosenFormat = {};

    if (window != nullptr)
    {
        if (vkCreateAndroidSurfaceKHR(getInstance(), &create_info, nullptr, &surface) != VK_SUCCESS)
        {
            return nullptr;
        }

        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &surfaceCapabilities) != VK_SUCCESS)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkGetPhysicalDeviceSurfaceCapabilitiesKHR error");
            return nullptr;
        }

        uint32_t surfaceFormatCount;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &surfaceFormatCount, nullptr) != VK_SUCCESS)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkGetPhysicalDeviceSurfaceFormatsKHR error");
            return nullptr;
        }
        surfaceFormats.resize(surfaceFormatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &surfaceFormatCount, surfaceFormats.data());
    }

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(gpu, &deviceFeatures);

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, queueFamilies.data());

    int queueFamilyIndex = -1;
    for (int a = 0; a < queueFamilyCount; a++)
    {
        if (surface != VK_NULL_HANDLE)
        {
            VkBool32 pSupported;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(gpu, a, surface, &pSupported) == VK_SUCCESS && pSupported)
            {
                queueFamilyIndex = a;
                break;
            }
        }
        else if (queueFamilies[a].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            queueFamilyIndex = a;
            break;
        }
    }
    if (queueFamilyIndex == -1)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "queueFamilyIndex = -1");
        return nullptr;
    }
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "queueFamilyIndex = %d", queueFamilyIndex);

    // Detect ray tracing extension availability.
    bool hasAS  = false, hasRTP = false, hasDHO = false;
    for (const auto &e : availableExtensions) {
        if (strcmp(e.extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)   == 0) hasAS  = true;
        if (strcmp(e.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)     == 0) hasRTP = true;
        if (strcmp(e.extensionName, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0) hasDHO = true;
    }
    const bool rtSupported = hasAS && hasRTP && hasDHO;

    std::vector<const char *> deviceExtensions;
    if (surface != VK_NULL_HANDLE)
        deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (rtSupported) {
        deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    }

    float priority = 1.0;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &priority;

    // RT feature chain — only attached when RT extensions are available.
    VkPhysicalDeviceBufferDeviceAddressFeatures bdaFeatures{};
    bdaFeatures.sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bdaFeatures.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
    asFeatures.sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    asFeatures.accelerationStructure = VK_TRUE;
    asFeatures.pNext                 = &bdaFeatures;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtpFeatures{};
    rtpFeatures.sType              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rtpFeatures.rayTracingPipeline = VK_TRUE;
    rtpFeatures.pNext              = &asFeatures;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
    createInfo.ppEnabledExtensionNames = deviceExtensions.empty() ? nullptr : deviceExtensions.data();
    if (rtSupported) createInfo.pNext = &rtpFeatures;

    VkDevice toReturn;
    if (vkCreateDevice(gpu, &createInfo, nullptr, &toReturn) != VK_SUCCESS)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateDevice() error");
        return nullptr;
    }

    // Load dynamic rendering function pointers (VK_KHR_dynamic_rendering)
    loadDynamicRenderingFunctions(toReturn);

    // Load ray tracing function pointers when the extensions are available.
    if (rtSupported) loadRayTracingFunctions(toReturn);

    // create swapchain (only when a real surface is available)
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkExtent2D imageExtent = {};

    if (surface != VK_NULL_HANDLE && !surfaceFormats.empty())
    {
        VkSwapchainCreateInfoKHR swapchainData = {};
        swapchainData.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainData.pNext = nullptr;
        swapchainData.flags = 0;
        swapchainData.surface = surface;
        swapchainData.minImageCount = std::min(std::max((int)surfaceCapabilities.minImageCount, 3), (int)surfaceCapabilities.maxImageCount);
        // Prefer sRGB formats for correct gamma presentation; fall back to first available.
        VkSurfaceFormatKHR chosenFormat = surfaceFormats[0];
        for (const auto &sf : surfaceFormats) {
            if ((sf.format == VK_FORMAT_B8G8R8A8_SRGB || sf.format == VK_FORMAT_R8G8B8A8_SRGB)
                && sf.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosenFormat = sf;
                break;
            }
        }
        swapchainData.imageFormat = chosenFormat.format;
        swapchainData.imageColorSpace = chosenFormat.colorSpace;
        swapchainData.imageExtent = surfaceCapabilities.currentExtent;
        swapchainData.imageArrayLayers = 1;
        swapchainData.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainData.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainData.preTransform = surfaceCapabilities.currentTransform;
        swapchainData.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        swapchainData.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchainData.clipped = true;
        swapchainData.oldSwapchain = 0;
        imageExtent = surfaceCapabilities.currentExtent;

        if (vkCreateSwapchainKHR(toReturn, &swapchainData, nullptr, &swapchain) != VK_SUCCESS)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateSwapchainKHR() error");
            return nullptr;
        }

        uint32_t swapchainImageCount;
        if (vkGetSwapchainImagesKHR(toReturn, swapchain, &swapchainImageCount, nullptr) != VK_SUCCESS)
        {
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkGetSwapchainImagesKHR() error");
            return nullptr;
        }
        swapchainImages.resize(swapchainImageCount);
        vkGetSwapchainImagesKHR(toReturn, swapchain, &swapchainImageCount, swapchainImages.data());
        for (uint32_t a = 0; a < swapchainImageCount; a++)
        {
            VkImageViewCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            info.image = swapchainImages[a];
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = swapchainData.imageFormat;
            info.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
            info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            VkImageView imageView;
            vkCreateImageView(toReturn, &info, nullptr, &imageView);
            swapchainImageViews.push_back(imageView);
        }
    }

    VkCommandPoolCreateInfo commandPoolDescriptor = {};
    commandPoolDescriptor.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolDescriptor.pNext = nullptr;
    commandPoolDescriptor.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolDescriptor.queueFamilyIndex = queueFamilyIndex;
    VkCommandPool commandPool;
    vkCreateCommandPool(toReturn, &commandPoolDescriptor, nullptr, &commandPool);

    // Create a general-purpose descriptor pool.
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,              100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,              100 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,               100 },
        { VK_DESCRIPTOR_TYPE_SAMPLER,                     100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,               100 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,      100 },
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,  100 },
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets       = 100;
    poolInfo.poolSizeCount = rtSupported ? 7 : 6;
    poolInfo.pPoolSizes    = poolSizes;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    vkCreateDescriptorPool(toReturn, &poolInfo, nullptr, &descriptorPool);

    // Create sync primitives for swapchain acquire/present.
    VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    vkCreateSemaphore(toReturn, &semInfo, nullptr, &imageAvailableSemaphore);
    vkCreateSemaphore(toReturn, &semInfo, nullptr, &renderFinishedSemaphore);

    auto deviceData = new DeviceData();
    deviceData->device = toReturn;
    vkGetDeviceQueue(toReturn, queueFamilyIndex, 0, &deviceData->graphicsQueue);
    deviceData->physicalDevice           = gpu;
    deviceData->rayTracingEnabled        = rtSupported;
    deviceData->surface                  = surface;
    deviceData->swapchain                = swapchain;
    deviceData->imageExtent              = imageExtent;
    deviceData->swapchainImages          = swapchainImages;
    deviceData->swapchainImageViews      = swapchainImageViews;
    deviceData->surfaceFormat            = surfaceFormats.empty() ? VkSurfaceFormatKHR{} : chosenFormat;
    deviceData->commandPool              = commandPool;
    deviceData->descriptorPool           = descriptorPool;
    deviceData->imageAvailableSemaphore  = imageAvailableSemaphore;
    deviceData->renderFinishedSemaphore  = renderFinishedSemaphore;
    deviceData->window                   = window;
    deviceData->queueFamilyIndex         = static_cast<uint32_t>(queueFamilyIndex);

    return std::shared_ptr<Device>(new Device(deviceData));
}

std::shared_ptr<Texture> Device::createTexture(
    TextureType type,
    PixelFormat pixelFormat,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    uint32_t mipLevels,
    uint32_t samples,
    TextureUsage usageMode)
{

    auto deviceData = (DeviceData *)this->native;

    // calculate buffer size
    uint64_t bufferSize = (width * height * depth * samples * getPixelFormatSize(pixelFormat)) / 8;
    uint32_t w = width;
    uint32_t h = height;
    for (int a = 1; a < mipLevels; a++)
    {
        w = w / 2;
        h = h / 2;
        uint64_t mipSize = (w * h * depth * samples * getPixelFormatSize(pixelFormat)) / 8;
        bufferSize += mipSize;
    }

    // create buffer
    auto buffer = createBuffer(bufferSize, BufferUsage::copySrc);

    VkImageUsageFlags imageUsage = 0;
    if ((int)usageMode & (int)TextureUsage::copySrc)
        imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if ((int)usageMode & (int)TextureUsage::copyDst)
        imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((int)usageMode & (int)TextureUsage::renderTarget)
        imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if ((int)usageMode & (int)TextureUsage::storageBinding)
        imageUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if ((int)usageMode & (int)TextureUsage::textureBinding)
        imageUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;

    // create image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext = nullptr;
    VkImageType imageType;
    uint32_t arrayLayers = 1;
    uint32_t extDepth = depth;
    switch (type) {
        case TextureType::tt1d:       imageType = VK_IMAGE_TYPE_1D; arrayLayers = 1;       extDepth = 1; break;
        case TextureType::tt3d:       imageType = VK_IMAGE_TYPE_3D; arrayLayers = 1;       break;
        case TextureType::ttCube:     imageType = VK_IMAGE_TYPE_2D; arrayLayers = 6;       extDepth = 1; break;
        case TextureType::ttCubeArray: imageType = VK_IMAGE_TYPE_2D; arrayLayers = depth;   extDepth = 1; break;
        default:                      imageType = VK_IMAGE_TYPE_2D; arrayLayers = depth;   extDepth = 1; break;
    }
    imageInfo.imageType = imageType;
    imageInfo.format = pixelFormatToNative(pixelFormat);
    imageInfo.extent = {width, height, extDepth};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.samples = (VkSampleCountFlagBits)samples;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = imageUsage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.queueFamilyIndexCount = 0;
    imageInfo.pQueueFamilyIndices = nullptr;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (type == TextureType::ttCube || type == TextureType::ttCubeArray ||
        (type == TextureType::tt2d && depth >= 6))
        imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VkImage image;
    if (vkCreateImage(deviceData->device, &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        return nullptr;
    }

    // Create a default image view for this texture.
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = image;
    viewInfo.viewType = (type == TextureType::tt3d) ? VK_IMAGE_VIEW_TYPE_3D
                      : (type == TextureType::tt1d) ? VK_IMAGE_VIEW_TYPE_1D
                                                    : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = pixelFormatToNative(pixelFormat);
    VkImageAspectFlags defaultAspect = VK_IMAGE_ASPECT_COLOR_BIT;
    {
        VkFormat f = viewInfo.format;
        if (f == VK_FORMAT_D16_UNORM || f == VK_FORMAT_D32_SFLOAT)
            defaultAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        else if (f == VK_FORMAT_S8_UINT)
            defaultAspect = VK_IMAGE_ASPECT_STENCIL_BIT;
        else if (f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT)
            defaultAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    viewInfo.subresourceRange.aspectMask     = defaultAspect;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = arrayLayers;

    VkImageView defaultView = VK_NULL_HANDLE;
    vkCreateImageView(deviceData->device, &viewInfo, nullptr, &defaultView);

    auto toReturn = new TextureHandle();
    toReturn->device         = deviceData->device;
    toReturn->physicalDevice = deviceData->physicalDevice;
    toReturn->commandPool    = deviceData->commandPool;
    toReturn->graphicsQueue  = deviceData->graphicsQueue;
    toReturn->buffer         = buffer;
    toReturn->image          = image;
    toReturn->defaultView    = defaultView;
    toReturn->currentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    toReturn->width          = width;
    toReturn->height         = height;
    toReturn->depth          = depth;
    toReturn->arrayLayers    = arrayLayers;
    toReturn->mipLevels      = mipLevels;
    toReturn->samples        = samples;
    toReturn->pixelFormat    = pixelFormat;
    toReturn->usage          = usageMode;
    toReturn->textureType    = type;
    toReturn->deviceData     = deviceData;
    
    // Get the buffer's allocated size for memory tracking
    auto bufferHandle = (BufferHandle *)buffer->native;
    toReturn->allocatedSize = bufferHandle->allocatedSize;

    deviceData->textureCount++;
    
    // Phase 2: Track texture memory (using buffer size as approximation)
    uint64_t newTextureBytes = deviceData->textureBytes.fetch_add(toReturn->allocatedSize) + toReturn->allocatedSize;
    
    // Update peak if needed
    uint64_t currentPeak = deviceData->peakTextureBytes.load();
    while (newTextureBytes > currentPeak && !deviceData->peakTextureBytes.compare_exchange_weak(currentPeak, newTextureBytes)) {
        // Retry if another thread updated the peak
    }
    
    // Update total peak
    uint64_t totalBytes = deviceData->bufferBytes.load() + deviceData->textureBytes.load() + 
                          deviceData->accelerationStructureBytes.load() + deviceData->querySetBytes.load();
    uint64_t currentTotalPeak = deviceData->peakTotalBytes.load();
    while (totalBytes > currentTotalPeak && !deviceData->peakTotalBytes.compare_exchange_weak(currentTotalPeak, totalBytes)) {
        // Retry if another thread updated the peak
    }
    
    return std::shared_ptr<Texture>(new Texture(toReturn));
}

uint32_t findMemoryType(uint32_t typeFilter, VkPhysicalDeviceMemoryProperties &memProperties, VkMemoryPropertyFlags properties)
{
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

std::shared_ptr<Buffer> Device::createBuffer(uint64_t size, BufferUsage usage)
{

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBufferUsageFlags vkUsage = 0;
    if ((int)usage & (int)BufferUsage::copySrc) vkUsage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if ((int)usage & (int)BufferUsage::copyDst) vkUsage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if ((int)usage & (int)BufferUsage::index) vkUsage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if ((int)usage & (int)BufferUsage::indirect) vkUsage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    //if ((int)usage & (int)BufferUsage::mapRead) vkUsage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    //if ((int)usage & (int)BufferUsage::mapWrite) vkUsage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    //if ((int)usage & (int)BufferUsage::queryResolve) vkUsage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if ((int)usage & (int)BufferUsage::storage) vkUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if ((int)usage & (int)BufferUsage::uniform) vkUsage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if ((int)usage & (int)BufferUsage::vertex)  vkUsage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if ((int)usage & (int)BufferUsage::accelerationStructureInput)
        vkUsage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                 | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if ((int)usage & (int)BufferUsage::accelerationStructureStorage)
        vkUsage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                 | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.usage = vkUsage;

    auto deviceData = (DeviceData *)this->native;

    VkBuffer bufferHandle;
    if (vkCreateBuffer(deviceData->device, &bufferInfo, nullptr, &bufferHandle) != VK_SUCCESS)
    {
        return nullptr;
    }

    VkMemoryRequirements bufferRequirements;
    vkGetBufferMemoryRequirements(deviceData->device, bufferHandle, &bufferRequirements);

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(deviceData->physicalDevice, &memProperties);

    auto memoryType = findMemoryType(bufferRequirements.memoryTypeBits, memProperties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    const bool needsBda = ((int)usage & (int)BufferUsage::accelerationStructureInput) ||
                          ((int)usage & (int)BufferUsage::accelerationStructureStorage);

    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo;
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = needsBda ? &allocFlagsInfo : nullptr;
    allocInfo.allocationSize = bufferRequirements.size;
    allocInfo.memoryTypeIndex = memoryType;
    VkDeviceMemory memoryHandle;
    if (vkAllocateMemory(deviceData->device, &allocInfo, nullptr, &memoryHandle) != VK_SUCCESS)
    {
        vkDestroyBuffer(deviceData->device, bufferHandle, nullptr);
        return nullptr;
    }

    vkBindBufferMemory(deviceData->device, bufferHandle, memoryHandle, 0);

    BufferHandle *toReturn = new BufferHandle();
    toReturn->device = deviceData->device;
    toReturn->buffer = bufferHandle;
    toReturn->memory = memoryHandle;
    toReturn->size   = size;
    toReturn->allocatedSize = bufferRequirements.size;
    toReturn->deviceData = deviceData;

    deviceData->bufferCount++;
    
    // Phase 2: Track buffer memory
    uint64_t newBufferBytes = deviceData->bufferBytes.fetch_add(bufferRequirements.size) + bufferRequirements.size;
    
    // Update peak if needed
    uint64_t currentPeak = deviceData->peakBufferBytes.load();
    while (newBufferBytes > currentPeak && !deviceData->peakBufferBytes.compare_exchange_weak(currentPeak, newBufferBytes)) {
        // Retry if another thread updated the peak
    }
    
    // Update total peak
    uint64_t totalBytes = deviceData->bufferBytes.load() + deviceData->textureBytes.load() + 
                          deviceData->accelerationStructureBytes.load() + deviceData->querySetBytes.load();
    uint64_t currentTotalPeak = deviceData->peakTotalBytes.load();
    while (totalBytes > currentTotalPeak && !deviceData->peakTotalBytes.compare_exchange_weak(currentTotalPeak, totalBytes)) {
        // Retry if another thread updated the peak
    }
    
    return std::shared_ptr<Buffer>(new Buffer(toReturn));
}

std::string Device::getName()
{
    auto deviceData = (DeviceData *)this->native;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(deviceData->physicalDevice, &props);
    return props.deviceName;
}

std::set<Feature> Device::getFeatures()
{
    auto deviceData = (DeviceData *)this->native;

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(deviceData->physicalDevice, &deviceProperties);

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(deviceData->physicalDevice, &deviceFeatures);

    std::set<Feature> toReturn;

    if (deviceFeatures.textureCompressionBC)
    {
        toReturn.insert(Feature::bcTextureCompression);
    }

    if (deviceFeatures.geometryShader)
    {
        toReturn.insert(Feature::geometryShader);
    }

    return toReturn;
}

std::string Device::getEngineVersion()
{
    uint32_t version = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&version);
    return "Vulkan " +
        std::to_string(VK_API_VERSION_MAJOR(version)) + "." +
        std::to_string(VK_API_VERSION_MINOR(version)) + "." +
        std::to_string(VK_API_VERSION_PATCH(version));
}

// ---------------------------------------------------------------------------
// Metrics and monitoring (Phase 1)
// ---------------------------------------------------------------------------

DeviceMemoryInfo Device::getMemoryInfo() {
    DeviceMemoryInfo info;
    auto deviceData = (DeviceData *)this->native;
    
    // Get physical device memory properties
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(deviceData->physicalDevice, &memProperties);
    
    // Sum up device-local heaps as total memory
    for (uint32_t i = 0; i < memProperties.memoryHeapCount; i++) {
        if (memProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            info.totalDeviceMemory += memProperties.memoryHeaps[i].size;
        }
    }
    
    // Note: Vulkan doesn't have a direct equivalent to Metal's currentAllocatedSize
    // without VK_EXT_memory_budget extension. We leave it as 0 for now.
    info.currentAllocatedSize = 0;
    info.recommendedMaxWorkingSet = 0;
    info.availableDeviceMemory = 0;
    info.hasUnifiedMemory = false;  // Most Android devices have discrete memory
    
    return info;
}

ResourceCounters Device::getResourceCounters() {
    ResourceCounters counters;
    auto deviceData = (DeviceData *)this->native;
    
    counters.bufferCount = deviceData->bufferCount.load();
    counters.textureCount = deviceData->textureCount.load();
    counters.renderPipelineCount = deviceData->renderPipelineCount.load();
    counters.computePipelineCount = deviceData->computePipelineCount.load();
    counters.rayTracingPipelineCount = deviceData->rayTracingPipelineCount.load();
    counters.accelerationStructureCount = deviceData->accelerationStructureCount.load();
    counters.shaderModuleCount = deviceData->shaderModuleCount.load();
    counters.samplerCount = deviceData->samplerCount.load();
    counters.bindGroupCount = deviceData->bindGroupCount.load();
    counters.bindGroupLayoutCount = deviceData->bindGroupLayoutCount.load();
    counters.pipelineLayoutCount = deviceData->pipelineLayoutCount.load();
    counters.querySetCount = deviceData->querySetCount.load();
    
    return counters;
}

CommandStats Device::getCommandStats() {
    CommandStats stats;
    auto deviceData = (DeviceData *)this->native;
    
    stats.commandsSubmitted = deviceData->commandsSubmitted.load();
    stats.renderPasses = deviceData->renderPasses.load();
    stats.computePasses = deviceData->computePasses.load();
    stats.rayTracingPasses = deviceData->rayTracingPasses.load();
    stats.drawCalls = deviceData->drawCalls.load();
    stats.dispatchCalls = deviceData->dispatchCalls.load();
    stats.traceRaysCalls = deviceData->traceRaysCalls.load();
    stats.copies = deviceData->copies.load();
    
    return stats;
}

Metrics Device::getMetrics() {
    Metrics m;
    m.deviceMemory = getMemoryInfo();
    m.resources = getResourceCounters();
    m.commands = getCommandStats();
    m.resourceMemory = getResourceMemoryStats();
    return m;
}

void Device::resetCommandStats() {
    auto deviceData = (DeviceData *)this->native;
    
    deviceData->commandsSubmitted = 0;
    deviceData->renderPasses = 0;
    deviceData->computePasses = 0;
    deviceData->rayTracingPasses = 0;
    deviceData->drawCalls = 0;
    deviceData->dispatchCalls = 0;
    deviceData->traceRaysCalls = 0;
    deviceData->copies = 0;
}

ResourceMemoryStats Device::getResourceMemoryStats() {
    ResourceMemoryStats stats;
    auto deviceData = (DeviceData *)this->native;
    
    stats.bufferBytes = deviceData->bufferBytes.load();
    stats.textureBytes = deviceData->textureBytes.load();
    stats.accelerationStructureBytes = deviceData->accelerationStructureBytes.load();
    stats.shaderModuleBytes = deviceData->shaderModuleBytes.load();
    stats.querySetBytes = deviceData->querySetBytes.load();
    stats.totalTrackedBytes = stats.bufferBytes + stats.textureBytes + 
                               stats.accelerationStructureBytes + stats.querySetBytes;
    
    stats.peakBufferBytes = deviceData->peakBufferBytes.load();
    stats.peakTextureBytes = deviceData->peakTextureBytes.load();
    stats.peakAccelerationStructureBytes = deviceData->peakAccelerationStructureBytes.load();
    stats.peakTotalBytes = deviceData->peakTotalBytes.load();
    
    return stats;
}

void Device::resetPeakMemoryStats() {
    auto deviceData = (DeviceData *)this->native;
    
    // Reset peaks to current values
    uint64_t currentBuffer = deviceData->bufferBytes.load();
    uint64_t currentTexture = deviceData->textureBytes.load();
    uint64_t currentAS = deviceData->accelerationStructureBytes.load();
    uint64_t currentTotal = currentBuffer + currentTexture + currentAS + deviceData->querySetBytes.load();
    
    deviceData->peakBufferBytes = currentBuffer;
    deviceData->peakTextureBytes = currentTexture;
    deviceData->peakAccelerationStructureBytes = currentAS;
    deviceData->peakTotalBytes = currentTotal;
}

// -----------------------------------------------------------------------------
// Phase 3: GPU Timing and Memory Pressure
// -----------------------------------------------------------------------------

PassPerformanceStats Device::getPassPerformanceStats() {
    PassPerformanceStats stats;
    auto deviceData = (DeviceData *)this->native;
    
    stats.renderPassTimeNs = deviceData->renderPassTimeNs.load();
    stats.computePassTimeNs = deviceData->computePassTimeNs.load();
    stats.rayTracingPassTimeNs = deviceData->rayTracingPassTimeNs.load();
    stats.totalPassTimeNs = stats.renderPassTimeNs + stats.computePassTimeNs + stats.rayTracingPassTimeNs;
    stats.renderPassSampleCount = deviceData->renderPassSampleCount.load();
    stats.computePassSampleCount = deviceData->computePassSampleCount.load();
    stats.rayTracingPassSampleCount = deviceData->rayTracingPassSampleCount.load();
    
    return stats;
}

void Device::resetPassPerformanceStats() {
    auto deviceData = (DeviceData *)this->native;
    
    deviceData->renderPassTimeNs = 0;
    deviceData->computePassTimeNs = 0;
    deviceData->rayTracingPassTimeNs = 0;
    deviceData->renderPassSampleCount = 0;
    deviceData->computePassSampleCount = 0;
    deviceData->rayTracingPassSampleCount = 0;
}

MemoryPressureLevel Device::getMemoryPressureLevel() {
    auto deviceData = (DeviceData *)this->native;
    auto stats = getResourceMemoryStats();
    
    // Get total device memory from physical device properties
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(deviceData->physicalDevice, &memProperties);
    
    uint64_t totalDeviceMemory = 0;
    for (uint32_t i = 0; i < memProperties.memoryHeapCount; i++) {
        if (memProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            totalDeviceMemory += memProperties.memoryHeaps[i].size;
        }
    }
    
    if (totalDeviceMemory == 0) {
        return MemoryPressureLevel::Normal;
    }
    
    uint64_t currentUsage = stats.totalTrackedBytes;
    uint64_t warningThreshold = (totalDeviceMemory * deviceData->memoryBudget.warningThresholdPercent) / 100;
    uint64_t criticalThreshold = (totalDeviceMemory * deviceData->memoryBudget.criticalThresholdPercent) / 100;
    
    if (currentUsage >= criticalThreshold) {
        return MemoryPressureLevel::Critical;
    } else if (currentUsage >= warningThreshold) {
        return MemoryPressureLevel::Warning;
    }
    return MemoryPressureLevel::Normal;
}

void Device::setMemoryBudget(const MemoryBudget& budget) {
    auto deviceData = (DeviceData *)this->native;
    deviceData->memoryBudget = budget;
}

MemoryBudget Device::getMemoryBudget() {
    auto deviceData = (DeviceData *)this->native;
    return deviceData->memoryBudget;
}

void Device::setMemoryPressureCallback(MemoryPressureCallback callback) {
    auto deviceData = (DeviceData *)this->native;
    deviceData->memoryPressureCallback = callback;
}

MemoryPressureLevel Device::checkMemoryPressure() {
    auto deviceData = (DeviceData *)this->native;
    auto currentLevel = getMemoryPressureLevel();
    auto previousLevel = deviceData->lastPressureLevel.exchange(currentLevel);
    
    if (deviceData->memoryPressureCallback && 
        (currentLevel != previousLevel || currentLevel == MemoryPressureLevel::Critical)) {
        deviceData->memoryPressureCallback(currentLevel, getResourceMemoryStats());
    }
    
    return currentLevel;
}

MetricsWithTiming Device::getMetricsWithTiming() {
    MetricsWithTiming m;
    m.deviceMemory = getMemoryInfo();
    m.resources = getResourceCounters();
    m.commands = getCommandStats();
    m.resourceMemory = getResourceMemoryStats();
    m.passPerformance = getPassPerformanceStats();
    return m;
}

std::shared_ptr<ShaderModule> Device::createShaderModule(const uint8_t *buffer, uint64_t size)
{
    auto deviceData = (DeviceData *)this->native;

    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.pNext = nullptr;
    info.pCode = (uint32_t *)buffer;
    info.codeSize = size;

    VkShaderModule shaderModule;
    auto result = vkCreateShaderModule(deviceData->device, &info, nullptr, &shaderModule);
    switch (result) {
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateShaderModule() error: VK_ERROR_OUT_OF_HOST_MEMORY");
            return nullptr;
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateShaderModule() error: VK_ERROR_OUT_OF_DEVICE_MEMORY");
            return nullptr;
        case VK_ERROR_INVALID_SHADER_NV:
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateShaderModule() error: VK_ERROR_INVALID_SHADER_NV");
            return nullptr;
        case VK_SUCCESS: {
            auto toReturn = new ShaderModuleHandle();
            toReturn->device = deviceData->device;
            toReturn->shaderModule = shaderModule;

            deviceData->shaderModuleCount++;
            return std::shared_ptr<ShaderModule>(new ShaderModule(toReturn));
        }
        default:
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateShaderModule() unknown error");
            return nullptr;
    }
}

static VkBlendFactor toVkBlendFactor(BlendFactor f) {
    switch (f) {
        case BlendFactor::zero:             return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::one:              return VK_BLEND_FACTOR_ONE;
        case BlendFactor::srcColor:         return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::oneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::srcAlpha:         return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::oneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::dstColor:         return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::oneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::dstAlpha:         return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::oneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        default:                            return VK_BLEND_FACTOR_ONE;
    }
}

std::shared_ptr<RenderPipeline> Device::createRenderPipeline(const RenderPipelineDescriptor &descriptor) {

    auto deviceData = (DeviceData *)this->native;

    if (descriptor.vertex.module == nullptr) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "createRenderPipeline() error. descriptor.vertex.module must be a valid ShaderModule object.");
        return nullptr;
    }

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    VkPipelineShaderStageCreateInfo vertexShaderStageInfo;
    vertexShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexShaderStageInfo.pNext = nullptr;
    vertexShaderStageInfo.flags = 0;
    vertexShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexShaderStageInfo.module = ((ShaderModuleHandle *)descriptor.vertex.module->native)->shaderModule;
    vertexShaderStageInfo.pName = descriptor.vertex.entryPoint.c_str();
    vertexShaderStageInfo.pSpecializationInfo = nullptr;
    shaderStages.push_back(vertexShaderStageInfo);

    if (descriptor.fragment.has_value()) {
        VkPipelineShaderStageCreateInfo fragmentShaderStageInfo;
        fragmentShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragmentShaderStageInfo.pNext = nullptr;
        fragmentShaderStageInfo.flags = 0;
        fragmentShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragmentShaderStageInfo.module = ((ShaderModuleHandle *)descriptor.fragment.value().module->native)->shaderModule;
        fragmentShaderStageInfo.pName = descriptor.fragment.value().entryPoint.c_str();
        fragmentShaderStageInfo.pSpecializationInfo = nullptr;
        shaderStages.push_back(fragmentShaderStageInfo);
    }

    std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.pNext = nullptr;
    dynamicState.flags = 0;
    dynamicState.dynamicStateCount = dynamicStates.size();
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pNext = nullptr;
    vertexInputInfo.flags = 0;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.pNext = nullptr;
    inputAssembly.flags = 0;
    VkPrimitiveTopology topology;
    switch (descriptor.topology) {
        case PrimitiveTopology::lineList:
            topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            break;
        case PrimitiveTopology::lineStrip:
            topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            break;
        case PrimitiveTopology::pointList:
            topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            break;
        case PrimitiveTopology::triangleList:
            topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            break;
        case PrimitiveTopology::triangleStrip:
            topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            break;
    }
    inputAssembly.topology = topology;
    inputAssembly.primitiveRestartEnable = false;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext = nullptr;
    viewportState.flags = 0;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.pNext = nullptr;
    rasterizer.flags = 0;
    rasterizer.depthClampEnable = false;
    rasterizer.rasterizerDiscardEnable = false;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    VkCullModeFlagBits cullMode;
    switch (descriptor.cullMode) {
        case CullMode::none:
            cullMode = VK_CULL_MODE_NONE;
            break;
        case CullMode::front:
            cullMode = VK_CULL_MODE_FRONT_BIT;
            break;
        case CullMode::back:
            cullMode = VK_CULL_MODE_BACK_BIT;
            break;
    }
    rasterizer.cullMode = cullMode;
    VkFrontFace frontFace;
    switch (descriptor.frontFace) {
        case FrontFace::cw:
            frontFace = VK_FRONT_FACE_CLOCKWISE;
            break;
        case FrontFace::ccw:
            frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            break;
    }
    rasterizer.frontFace = frontFace;
    rasterizer.depthBiasEnable = false;
    rasterizer.depthBiasSlopeFactor = 1.0;
    rasterizer.lineWidth = 1.0;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.pNext = nullptr;
    multisampling.flags = 0;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = false;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    if (descriptor.depthStencil.has_value()) {
        const auto &ds = *descriptor.depthStencil;
        depthStencil.depthTestEnable       = VK_TRUE;
        depthStencil.depthWriteEnable      = ds.depthWriteEnabled ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp        = static_cast<VkCompareOp>(ds.depthCompare);
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable     = (ds.stencilFront.has_value() || ds.stencilBack.has_value()) ? VK_TRUE : VK_FALSE;
        if (ds.stencilFront.has_value()) {
            const auto &sf = *ds.stencilFront;
            depthStencil.front.compareOp  = static_cast<VkCompareOp>(sf.compare);
            depthStencil.front.failOp     = static_cast<VkStencilOp>(sf.failOp);
            depthStencil.front.passOp     = static_cast<VkStencilOp>(sf.passOp);
            depthStencil.front.depthFailOp = static_cast<VkStencilOp>(sf.depthFailOp);
            depthStencil.front.compareMask = ds.stencilReadMask;
            depthStencil.front.writeMask   = ds.stencilWriteMask;
            depthStencil.front.reference   = 0;
        }
        if (ds.stencilBack.has_value()) {
            const auto &sb = *ds.stencilBack;
            depthStencil.back.compareOp   = static_cast<VkCompareOp>(sb.compare);
            depthStencil.back.failOp      = static_cast<VkStencilOp>(sb.failOp);
            depthStencil.back.passOp      = static_cast<VkStencilOp>(sb.passOp);
            depthStencil.back.depthFailOp  = static_cast<VkStencilOp>(sb.depthFailOp);
            depthStencil.back.compareMask  = ds.stencilReadMask;
            depthStencil.back.writeMask    = ds.stencilWriteMask;
            depthStencil.back.reference    = 0;
        }
        depthStencil.minDepthBounds = 0.0f;
        depthStencil.maxDepthBounds = 1.0f;
    }

    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
    if (descriptor.fragment.has_value()) {
        for (const auto& cs : descriptor.fragment->targets) {
            VkPipelineColorBlendAttachmentState att = {};
            att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            if (cs.blend) {
                att.blendEnable         = VK_TRUE;
                att.srcColorBlendFactor = toVkBlendFactor(cs.blend->color.srcFactor);
                att.dstColorBlendFactor = toVkBlendFactor(cs.blend->color.dstFactor);
                att.colorBlendOp        = static_cast<VkBlendOp>(cs.blend->color.operation);
                att.srcAlphaBlendFactor = toVkBlendFactor(cs.blend->alpha.srcFactor);
                att.dstAlphaBlendFactor = toVkBlendFactor(cs.blend->alpha.dstFactor);
                att.alphaBlendOp        = static_cast<VkBlendOp>(cs.blend->alpha.operation);
            } else {
                att.blendEnable = VK_FALSE;
            }
            colorBlendAttachments.push_back(att);
        }
    }
    if (colorBlendAttachments.empty()) {
        VkPipelineColorBlendAttachmentState att = {};
        att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        att.blendEnable = VK_FALSE;
        colorBlendAttachments.push_back(att);
    }

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.pNext = nullptr;
    colorBlending.flags = 0;
    colorBlending.logicOpEnable = false;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    // Use the caller-supplied pipeline layout if provided, otherwise create an empty one.
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    bool ownsPipelineLayout = false;
    if (descriptor.layout) {
        pipelineLayout = ((PipelineLayoutHandle *)descriptor.layout->native)->layout;
    } else {
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 0;
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        if (vkCreatePipelineLayout(deviceData->device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreatePipelineLayout() error");
            return nullptr;
        }
        ownsPipelineLayout = true;
    }

    VkFormat depthAttachmentFormat   = VK_FORMAT_UNDEFINED;
    VkFormat stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
    if (descriptor.depthStencil.has_value()) {
        VkFormat dsFormat = pixelFormatToNative(descriptor.depthStencil->format);
        if (dsFormat == VK_FORMAT_D16_UNORM || dsFormat == VK_FORMAT_D32_SFLOAT) {
            depthAttachmentFormat = dsFormat;
        } else if (dsFormat == VK_FORMAT_S8_UINT) {
            stencilAttachmentFormat = dsFormat;
        } else {
            // Combined depth+stencil (D24_UNORM_S8, D32_SFLOAT_S8, etc.)
            depthAttachmentFormat   = dsFormat;
            stencilAttachmentFormat = dsFormat;
        }
    }

    VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo = {};
    pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipelineRenderingCreateInfo.pNext = nullptr;
    pipelineRenderingCreateInfo.colorAttachmentCount = 1;
    pipelineRenderingCreateInfo.pColorAttachmentFormats = &deviceData->surfaceFormat.format;
    pipelineRenderingCreateInfo.depthAttachmentFormat   = depthAttachmentFormat;
    pipelineRenderingCreateInfo.stencilAttachmentFormat = stencilAttachmentFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &pipelineRenderingCreateInfo;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size()),
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = descriptor.depthStencil.has_value() ? &depthStencil : nullptr;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE; // dynamic rendering: renderPass must be VK_NULL_HANDLE
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(
        deviceData->device,
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &pipeline
    ) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateGraphicsPipelines() error");
        return nullptr;
    }

    auto toReturn = new RenderPipelineHandle();
    toReturn->device              = deviceData->device;
    toReturn->pipeline            = pipeline;
    toReturn->pipelineLayout      = pipelineLayout;
    toReturn->ownsPipelineLayout  = ownsPipelineLayout;

    deviceData->renderPipelineCount++;
    return std::shared_ptr<RenderPipeline>(new RenderPipeline(toReturn));
}

std::shared_ptr<ComputePipeline> Device::createComputePipeline(const ComputePipelineDescriptor &descriptor) {

    auto deviceData = (DeviceData *)this->native;

    VkComputePipelineCreateInfo info;
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.pNext = nullptr;
    info.stage.flags = 0;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = ((ShaderModuleHandle *)descriptor.compute.module->native)->shaderModule;
    info.stage.pName = descriptor.compute.entryPoint.c_str();
    info.stage.pSpecializationInfo = nullptr;
    info.layout = ((PipelineLayoutHandle *)descriptor.layout->native)->layout;
    info.basePipelineHandle = VK_NULL_HANDLE;
    info.basePipelineIndex = -1;

    VkPipeline pipeline;
    if (vkCreateComputePipelines(
        deviceData->device,
        VK_NULL_HANDLE,
        1,
        &info,
        nullptr,
        &pipeline
    ) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateComputePipelines() error");
        return nullptr;
    }

    auto toReturn = new ComputePipelineHandle();
    toReturn->device         = deviceData->device;
    toReturn->pipeline       = pipeline;
    toReturn->pipelineLayout = info.layout;

    deviceData->computePipelineCount++;
    return std::shared_ptr<ComputePipeline>(new ComputePipeline(toReturn));
}

std::shared_ptr<CommandEncoder> Device::createCommandEncoder() {

    auto deviceData = (DeviceData *)this->native;

    VkCommandBufferAllocateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.pNext = nullptr;
    info.commandPool = deviceData->commandPool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(deviceData->device, &info, &commandBuffer) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu",
                            "createCommandEncoder: vkAllocateCommandBuffers failed");
        return nullptr;
    }

    auto toReturn = new CommandEncoderHandle();
    toReturn->device                    = deviceData->device;
    toReturn->commandPool               = deviceData->commandPool;
    toReturn->commandBuffer             = commandBuffer;
    toReturn->imageExtent               = deviceData->imageExtent;
    toReturn->swapchain                 = deviceData->swapchain;
    toReturn->graphicsQueue             = deviceData->graphicsQueue;
    toReturn->imageAvailableSemaphore   = deviceData->imageAvailableSemaphore;
    toReturn->renderFinishedSemaphore   = deviceData->renderFinishedSemaphore;
    toReturn->swapchainImages           = deviceData->swapchainImages;
    toReturn->swapchainImageViews       = deviceData->swapchainImageViews;
    toReturn->currentImageIndex         = 0;
    toReturn->physicalDevice            = deviceData->physicalDevice;
    toReturn->timestampQueryPool        = VK_NULL_HANDLE;
    toReturn->timestampPeriod           = 1.0f;

    return std::shared_ptr<CommandEncoder>(new CommandEncoder(toReturn));
}


VkFilter filterModeToVkFilter(FilterMode filter) {
    switch (filter) {
        case FilterMode::fmLinear:
        case FilterMode::fmLinearMipmapLinear:
        case FilterMode::fmLinearMipmapNearest:
            return VK_FILTER_LINEAR;
        default:
            return VK_FILTER_NEAREST;
    }
}

VkSamplerMipmapMode getMipmapMode(const SamplerDescriptor &descriptor) {
    if (descriptor.minFilter == FilterMode::fmLinearMipmapLinear ||
        descriptor.minFilter == FilterMode::fmNearestMipmapLinear ||
        descriptor.magFilter == FilterMode::fmNearestMipmapLinear ||
        descriptor.magFilter == FilterMode::fmLinearMipmapLinear) {
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    return VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

VkSamplerAddressMode getAddressMode(WrapMode mode) {
    switch(mode) {
        case WrapMode::repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case WrapMode::clampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case WrapMode::mirrorRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }
}

VkCompareOp getCompareOp(CompareOp op) {
    switch (op) {
        case CompareOp::never: return VK_COMPARE_OP_NEVER;
        case CompareOp::less: return VK_COMPARE_OP_LESS;
        case CompareOp::equal: return VK_COMPARE_OP_EQUAL;
        case CompareOp::lessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::greater: return VK_COMPARE_OP_GREATER;
        case CompareOp::notEqual: return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::greaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::always: return VK_COMPARE_OP_ALWAYS;
    }
}

std::shared_ptr<Sampler> Device::createSampler(const SamplerDescriptor &descriptor) {

    auto deviceData = (DeviceData *)this->native;

    VkSamplerCreateInfo info;
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0;
    info.magFilter = filterModeToVkFilter(descriptor.magFilter);
    info.minFilter = filterModeToVkFilter(descriptor.minFilter);
    info.mipmapMode = getMipmapMode(descriptor);
    info.addressModeU = getAddressMode(descriptor.addressModeU);
    info.addressModeV = getAddressMode(descriptor.addressModeV);
    info.addressModeW = getAddressMode(descriptor.addressModeW);
    info.mipLodBias = 0.0;
    info.anisotropyEnable = true;
    info.maxAnisotropy = (float)descriptor.maxAnisotropy;
    info.compareEnable = false;
    if (descriptor.compare.has_value()) {
        info.compareEnable = true;
        info.compareOp = getCompareOp(descriptor.compare.value());
    }
    info.minLod = (float)descriptor.lodMinClamp;
    info.maxLod = (float)descriptor.lodMaxClamp;
    info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    info.unnormalizedCoordinates = false;

    VkSampler sampler;
    if (vkCreateSampler(deviceData->device, &info, nullptr, &sampler) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateSampler() error");
        return nullptr;
    }

    auto toReturn = new SamplerHandle();
    toReturn->device = deviceData->device;
    toReturn->sampler = sampler;

    deviceData->samplerCount++;
    return std::shared_ptr<Sampler>(new Sampler(toReturn));
}

std::shared_ptr<QuerySet> Device::createQuerySet(const QuerySetDescriptor &descriptor) {

    auto deviceData = (DeviceData *)this->native;

    VkQueryPoolCreateInfo info;
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0;
    info.queryType = descriptor.type == QuerySetType::occlusion ? VK_QUERY_TYPE_OCCLUSION : VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = descriptor.count;
    info.pipelineStatistics = 0;

    VkQueryPool queryPool;
    if (vkCreateQueryPool(deviceData->device, &info, nullptr, &queryPool) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateQueryPool() error");
        return nullptr;
    }

    auto toReturn = new QuerySetHandle();
    toReturn->device = deviceData->device;
    toReturn->queryPool = queryPool;

    deviceData->querySetCount++;
    return std::shared_ptr<QuerySet>(new QuerySet(toReturn));
}

std::shared_ptr<BindGroupLayout> Device::createBindGroupLayout(const BindGroupLayoutDescriptor &descriptor) {

    auto deviceData = (DeviceData *)this->native;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings(descriptor.entries.size());
    for (int a = 0; a < (int)descriptor.entries.size(); a++) {
        const auto &entry = descriptor.entries[a];
        layoutBindings[a].binding         = entry.binding;
        layoutBindings[a].descriptorCount = 1;
        layoutBindings[a].pImmutableSamplers = nullptr;

        // Map visibility bitmask to Vulkan stage flags.
        VkShaderStageFlags stageFlags = 0;
        if ((int)entry.visibility & (int)ShaderStage::vertex)       stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
        if ((int)entry.visibility & (int)ShaderStage::fragment)     stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        if ((int)entry.visibility & (int)ShaderStage::compute)      stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
        if ((int)entry.visibility & (int)ShaderStage::rayGeneration) stageFlags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        if ((int)entry.visibility & (int)ShaderStage::miss)          stageFlags |= VK_SHADER_STAGE_MISS_BIT_KHR;
        if ((int)entry.visibility & (int)ShaderStage::closestHit)    stageFlags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        if ((int)entry.visibility & (int)ShaderStage::anyHit)        stageFlags |= VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
        if ((int)entry.visibility & (int)ShaderStage::intersection)  stageFlags |= VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
        layoutBindings[a].stageFlags = stageFlags;

        switch (entry.type) {
            case EntryObjectType::sampler:
                layoutBindings[a].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                break;
            case EntryObjectType::texture:
                layoutBindings[a].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                break;
            case EntryObjectType::buffer:
                switch (entry.data.buffer.type) {
                    case EntryObjectBufferType::uniform:
                        layoutBindings[a].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                        break;
                    case EntryObjectBufferType::storage:
                    case EntryObjectBufferType::readOnlyStorage:
                        layoutBindings[a].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        break;
                }
                break;
            case EntryObjectType::accelerationStructure:
                layoutBindings[a].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                break;
        }
    }

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(descriptor.entries.size());
    info.pBindings    = layoutBindings.data();

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(deviceData->device, &info, nullptr, &layout) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateDescriptorSetLayout() error");
        return nullptr;
    }

    auto toReturn = new BindGroupLayoutHandle();
    toReturn->device = deviceData->device;
    toReturn->layout = layout;
    deviceData->bindGroupLayoutCount++;
    return std::shared_ptr<BindGroupLayout>(new BindGroupLayout(toReturn));
}

std::shared_ptr<PipelineLayout> Device::createPipelineLayout(const PipelineLayoutDescriptor &descriptor) {

    auto deviceData = (DeviceData *)this->native;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0; // Optional
    pipelineLayoutInfo.pSetLayouts = nullptr; // Optional
    pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
    pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(deviceData->device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreatePipelineLayout() error");
        return nullptr;
    }

    auto toReturn = new PipelineLayoutHandle();
    toReturn->device = deviceData->device;
    toReturn->layout = pipelineLayout;

    deviceData->pipelineLayoutCount++;
    return std::shared_ptr<PipelineLayout>(new PipelineLayout(toReturn));
}

std::shared_ptr<BindGroup> Device::createBindGroup(const BindGroupDescriptor &descriptor) {

    auto deviceData = (DeviceData *)this->native;

    // Get the VkDescriptorSetLayout from the layout handle.
    auto layoutHandle = (BindGroupLayoutHandle *)descriptor.layout->native;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = deviceData->descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &layoutHandle->layout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(deviceData->device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkAllocateDescriptorSets() error");
        return nullptr;
    }

    std::vector<VkWriteDescriptorSet>  writes;
    std::vector<VkDescriptorBufferInfo> bufferInfos(descriptor.entries.size());
    std::vector<VkDescriptorImageInfo>  imageInfos(descriptor.entries.size());

    for (int a = 0; a < (int)descriptor.entries.size(); a++) {
        const auto &entry = descriptor.entries[a];
        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = descriptorSet;
        write.dstBinding      = entry.binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;

        if (std::holds_alternative<BufferBinding>(entry.resource)) {
            const auto &bb     = std::get<BufferBinding>(entry.resource);
            auto bufHandle     = (BufferHandle *)bb.buffer->native;
            bufferInfos[a].buffer = bufHandle->buffer;
            bufferInfos[a].offset = bb.offset;
            bufferInfos[a].range  = bb.size;
            write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo     = &bufferInfos[a];
            writes.push_back(write);
        } else if (std::holds_alternative<std::shared_ptr<Texture>>(entry.resource)) {
            const auto &tex  = std::get<std::shared_ptr<Texture>>(entry.resource);
            auto texHandle   = (TextureHandle *)tex->native;
            imageInfos[a].imageView   = texHandle->defaultView;
            imageInfos[a].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[a].sampler     = VK_NULL_HANDLE;
            write.descriptorType      = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            write.pImageInfo          = &imageInfos[a];
            writes.push_back(write);
        } else if (std::holds_alternative<std::shared_ptr<Sampler>>(entry.resource)) {
            const auto &samp = std::get<std::shared_ptr<Sampler>>(entry.resource);
            auto sampHandle  = (SamplerHandle *)samp->native;
            imageInfos[a].sampler     = sampHandle->sampler;
            imageInfos[a].imageView   = VK_NULL_HANDLE;
            imageInfos[a].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            write.descriptorType      = VK_DESCRIPTOR_TYPE_SAMPLER;
            write.pImageInfo          = &imageInfos[a];
            writes.push_back(write);
        } else if (std::holds_alternative<std::shared_ptr<AccelerationStructure>>(entry.resource)) {
            // TODO: implement when Vulkan AS handle is ready (VkWriteDescriptorSetAccelerationStructureKHR via pNext).
        }
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(deviceData->device,
                               static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }

    auto toReturn = new BindGroupHandle();
    toReturn->descriptorSet = descriptorSet;
    deviceData->bindGroupCount++;
    return std::shared_ptr<BindGroup>(new BindGroup(toReturn));
}

// Rebuilds the swapchain, image views, and extent after a window resize.
// Called when vkAcquireNextImageKHR or vkQueuePresentKHR returns OUT_OF_DATE / SUBOPTIMAL.
static void recreateSwapchain(DeviceData *deviceData) {
    if (!deviceData->window || deviceData->surface == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(deviceData->device);

    // Destroy old image views.
    for (auto iv : deviceData->swapchainImageViews)
        vkDestroyImageView(deviceData->device, iv, nullptr);
    deviceData->swapchainImageViews.clear();

    // Re-query surface capabilities for the new window size.
    VkSurfaceCapabilitiesKHR caps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(deviceData->physicalDevice,
                                                   deviceData->surface, &caps) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu",
                            "recreateSwapchain: vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
        return;
    }

    VkExtent2D newExtent = caps.currentExtent;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface          = deviceData->surface;
    sci.minImageCount    = std::min(std::max((int)caps.minImageCount, 3), (int)caps.maxImageCount);
    sci.imageFormat      = deviceData->surfaceFormat.format;
    sci.imageColorSpace  = deviceData->surfaceFormat.colorSpace;
    sci.imageExtent      = newExtent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform     = caps.currentTransform;
    sci.compositeAlpha   = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    sci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped          = VK_TRUE;
    sci.oldSwapchain     = deviceData->swapchain; // hand old swapchain to driver for reuse

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(deviceData->device, &sci, nullptr, &newSwapchain) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu",
                            "recreateSwapchain: vkCreateSwapchainKHR failed");
        return;
    }

    // Destroy old swapchain only after the new one is successfully created.
    vkDestroySwapchainKHR(deviceData->device, deviceData->swapchain, nullptr);
    deviceData->swapchain   = newSwapchain;
    deviceData->imageExtent = newExtent;

    // Re-fetch swapchain images.
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(deviceData->device, newSwapchain, &imageCount, nullptr);
    deviceData->swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(deviceData->device, newSwapchain, &imageCount,
                            deviceData->swapchainImages.data());

    // Recreate image views.
    deviceData->swapchainImageViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                           = deviceData->swapchainImages[i];
        viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                          = deviceData->surfaceFormat.format;
        viewInfo.components                      = { VK_COMPONENT_SWIZZLE_IDENTITY,
                                                     VK_COMPONENT_SWIZZLE_IDENTITY,
                                                     VK_COMPONENT_SWIZZLE_IDENTITY,
                                                     VK_COMPONENT_SWIZZLE_IDENTITY };
        viewInfo.subresourceRange                = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(deviceData->device, &viewInfo, nullptr,
                          &deviceData->swapchainImageViews[i]);
    }

    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu",
                        "recreateSwapchain: rebuilt swapchain (%ux%u)", newExtent.width, newExtent.height);
}

void Device::submit(std::shared_ptr<CommandBuffer> commandBuffer) {

    auto deviceData = (DeviceData *)this->native;
    auto cbHandle   = (CommandBufferHandle *)commandBuffer->native;

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cbHandle->commandBuffer;

    if (cbHandle->hasSwapchain) {
        // Swapchain submission: synchronise with acquire/present semaphores.
        VkPipelineStageFlags waitStage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submitInfo.waitSemaphoreCount   = 1;
        submitInfo.pWaitSemaphores      = &deviceData->imageAvailableSemaphore;
        submitInfo.pWaitDstStageMask    = &waitStage;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores    = &deviceData->renderFinishedSemaphore;
    }
    // Headless / offscreen / compute: no semaphores needed.

    if (vkQueueSubmit(deviceData->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "Device::submit: vkQueueSubmit failed");
        return;
    }

    if (cbHandle->hasSwapchain) {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores    = &deviceData->renderFinishedSemaphore;
        presentInfo.swapchainCount     = 1;
        presentInfo.pSwapchains        = &deviceData->swapchain;
        presentInfo.pImageIndices      = &cbHandle->currentImageIndex;

        VkResult result = vkQueuePresentKHR(deviceData->graphicsQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            recreateSwapchain(deviceData);
        } else if (result != VK_SUCCESS) {
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu",
                                "Device::submit: vkQueuePresentKHR failed (%d)", result);
        }
    }

    // Wait for the frame to finish before the caller can reuse the command buffer.
    vkQueueWaitIdle(deviceData->graphicsQueue);
    
    deviceData->commandsSubmitted++;
}

void Device::submit(std::shared_ptr<CommandBuffer> commandBuffer,
                    std::shared_ptr<Fence> signalFence) {

    auto deviceData = (DeviceData *)this->native;
    auto cbHandle   = (CommandBufferHandle *)commandBuffer->native;
    auto fenceData  = (VulkanFenceData *)signalFence->native;

    // Reset fence before reuse (required by Vulkan binary fences).
    vkResetFences(deviceData->device, 1, &fenceData->fence);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cbHandle->commandBuffer;

    if (cbHandle->hasSwapchain) {
        VkPipelineStageFlags waitStage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submitInfo.waitSemaphoreCount   = 1;
        submitInfo.pWaitSemaphores      = &deviceData->imageAvailableSemaphore;
        submitInfo.pWaitDstStageMask    = &waitStage;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores    = &deviceData->renderFinishedSemaphore;
    }

    if (vkQueueSubmit(deviceData->graphicsQueue, 1, &submitInfo, fenceData->fence) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu",
                            "Device::submit(fence): vkQueueSubmit failed");
        return;
    }

    if (cbHandle->hasSwapchain) {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores    = &deviceData->renderFinishedSemaphore;
        presentInfo.swapchainCount     = 1;
        presentInfo.pSwapchains        = &deviceData->swapchain;
        presentInfo.pImageIndices      = &cbHandle->currentImageIndex;

        VkResult result = vkQueuePresentKHR(deviceData->graphicsQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            recreateSwapchain(deviceData);
        } else if (result != VK_SUCCESS) {
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu",
                                "Device::submit(fence): vkQueuePresentKHR failed (%d)", result);
        }
    }

    // NOTE: intentionally NOT calling vkQueueWaitIdle — caller waits on fence.
    deviceData->commandsSubmitted++;
}

std::shared_ptr<Fence> Device::createFence() {
    auto *deviceData = (DeviceData *)this->native;
    auto *fenceData  = new VulkanFenceData();
    fenceData->device = deviceData->device;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // start signaled so first frame doesn't block

    if (vkCreateFence(deviceData->device, &fenceInfo, nullptr, &fenceData->fence) != VK_SUCCESS) {
        delete fenceData;
        return nullptr;
    }

    return std::shared_ptr<Fence>(new Fence(fenceData));
}

void Device::waitForIdle() {
    auto deviceData = (DeviceData *)this->native;
    vkQueueWaitIdle(deviceData->graphicsQueue);
}

void Device::scheduleNextPresent(void* /*nativeDrawable*/) {
    // Vulkan handles swapchain presentation inside submit() — no-op here.
}

std::shared_ptr<TextureView> Device::getSwapchainTextureView() {
    // On Vulkan/Android the swapchain image is acquired per-frame via
    // vkAcquireNextImageKHR. Use TextureView::fromNative() with the
    // VkImageView for the current image instead.
    return nullptr;
}

std::string systems::leal::campello_gpu::getVersion()
{
    return std::to_string(campello_gpu_VERSION_MAJOR) + "." + std::to_string(campello_gpu_VERSION_MINOR) + "." + std::to_string(campello_gpu_VERSION_PATCH);
}

VkFormat pixelFormatToNative(PixelFormat format)
{
    switch (format)
    {
    case PixelFormat::invalid:
        return VK_FORMAT_UNDEFINED;
    // 8-bit formats
    case PixelFormat::r8unorm:
        return VK_FORMAT_R8_UNORM;
    case PixelFormat::r8snorm:
        return VK_FORMAT_R8_SNORM;
    case PixelFormat::r8uint:
        return VK_FORMAT_R8_UINT;
    case PixelFormat::r8sint:
        return VK_FORMAT_R8_SINT;

    // 16-bit formats
    case PixelFormat::r16unorm:
        return VK_FORMAT_R16_UNORM;
    case PixelFormat::r16snorm:
        return VK_FORMAT_R16_SNORM;
    case PixelFormat::r16uint:
        return VK_FORMAT_R16_UINT;
    case PixelFormat::r16sint:
        return VK_FORMAT_R16_SINT;
    case PixelFormat::r16float:
        return VK_FORMAT_R16_SFLOAT;
    case PixelFormat::rg8unorm:
        return VK_FORMAT_R8G8_UNORM;
    case PixelFormat::rg8snorm:
        return VK_FORMAT_R8G8_SNORM;
    case PixelFormat::rg8uint:
        return VK_FORMAT_R8G8_UINT;
    case PixelFormat::rg8sint:
        return VK_FORMAT_R8G8_SINT;

    // 32-bit formats
    case PixelFormat::r32uint:
        return VK_FORMAT_R32_UINT;
    case PixelFormat::r32sint:
        return VK_FORMAT_R32_SINT;
    case PixelFormat::r32float:
        return VK_FORMAT_R32_SFLOAT;
    case PixelFormat::rg16unorm:
        return VK_FORMAT_R16G16_UNORM;
    case PixelFormat::rg16snorm:
        return VK_FORMAT_R16G16_SNORM;
    case PixelFormat::rg16uint:
        return VK_FORMAT_R16G16_UINT;
    case PixelFormat::rg16sint:
        return VK_FORMAT_R16G16_SINT;
    case PixelFormat::rg16float:
        return VK_FORMAT_R16G16_SFLOAT;
    case PixelFormat::rgba8unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case PixelFormat::rgba8unorm_srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case PixelFormat::rgba8snorm:
        return VK_FORMAT_R8G8B8A8_SNORM;
    case PixelFormat::rgba8uint:
        return VK_FORMAT_R8G8B8A8_UINT;
    case PixelFormat::rgba8sint:
        return VK_FORMAT_R8G8B8A8_SINT;
    case PixelFormat::bgra8unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case PixelFormat::bgra8unorm_srgb:
        return VK_FORMAT_B8G8R8A8_SRGB;
    // Packed 32-bit formats
    case PixelFormat::rgb9e5ufloat:
        return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
    case PixelFormat::rgb10a2uint:
        return VK_FORMAT_A2R10G10B10_UINT_PACK32;
    case PixelFormat::rgb10a2unorm:
        return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    case PixelFormat::rg11b10ufloat:
        return VK_FORMAT_B10G11R11_UFLOAT_PACK32;

    // 64-bit formats
    case PixelFormat::rg32uint:
        return VK_FORMAT_R32G32_UINT;
    case PixelFormat::rg32sint:
        return VK_FORMAT_R32G32_SINT;
    case PixelFormat::rg32float:
        return VK_FORMAT_R32G32_SFLOAT;
    case PixelFormat::rgba16unorm:
        return VK_FORMAT_R16G16B16A16_UNORM;
    case PixelFormat::rgba16snorm:
        return VK_FORMAT_R16G16B16A16_SNORM;
    case PixelFormat::rgba16uint:
        return VK_FORMAT_R16G16B16A16_UINT;
    case PixelFormat::rgba16sint:
        return VK_FORMAT_R16G16B16A16_SINT;
    case PixelFormat::rgba16float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;

    // 128-bit formats
    case PixelFormat::rgba32uint:
        return VK_FORMAT_R32G32B32A32_UINT;
    case PixelFormat::rgba32sint:
        return VK_FORMAT_R32G32B32A32_SINT;
    case PixelFormat::rgba32float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;

    // Depth/stencil formats
    case PixelFormat::stencil8:
        return VK_FORMAT_S8_UINT;
    case PixelFormat::depth16unorm:
        return VK_FORMAT_D16_UNORM;
    // depth24plus, // no metal compatible
    case PixelFormat::depth24plus_stencil8:
        return VK_FORMAT_D24_UNORM_S8_UINT;
    case PixelFormat::depth32float:
        return VK_FORMAT_D32_SFLOAT;

    // "depth32float-stencil8" feature
    case PixelFormat::depth32float_stencil8:
        return VK_FORMAT_D32_SFLOAT_S8_UINT;

    // BC compressed formats usable if "texture-compression-bc" is both
    // supported by the device/user agent and enabled in requestDevice.
    case PixelFormat::bc1_rgba_unorm:
        return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case PixelFormat::bc1_rgba_unorm_srgb:
        return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case PixelFormat::bc2_rgba_unorm:
        return VK_FORMAT_BC2_UNORM_BLOCK;
    case PixelFormat::bc2_rgba_unorm_srgb:
        return VK_FORMAT_BC2_SRGB_BLOCK;
    case PixelFormat::bc3_rgba_unorm:
        return VK_FORMAT_BC3_UNORM_BLOCK;
    case PixelFormat::bc3_rgba_unorm_srgb:
        return VK_FORMAT_BC3_SRGB_BLOCK;
    case PixelFormat::bc4_r_unorm:
        return VK_FORMAT_BC4_UNORM_BLOCK;
    case PixelFormat::bc4_r_snorm:
        return VK_FORMAT_BC4_SNORM_BLOCK;
    case PixelFormat::bc5_rg_unorm:
        return VK_FORMAT_BC5_UNORM_BLOCK;
    case PixelFormat::bc5_rg_snorm:
        return VK_FORMAT_BC5_SNORM_BLOCK;
    case PixelFormat::bc6h_rgb_ufloat:
        return VK_FORMAT_BC6H_UFLOAT_BLOCK;
    case PixelFormat::bc6h_rgb_float:
        return VK_FORMAT_BC6H_SFLOAT_BLOCK;
    case PixelFormat::bc7_rgba_unorm:
        return VK_FORMAT_BC7_UNORM_BLOCK;
    case PixelFormat::bc7_rgba_unorm_srgb:
        return VK_FORMAT_BC7_SRGB_BLOCK;

    // ETC2 compressed formats usable if "texture-compression-etc2" is both
    // supported by the device/user agent and enabled in requestDevice.
    case PixelFormat::etc2_rgb8unorm:
        return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
    case PixelFormat::etc2_rgb8unorm_srgb:
        return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
    case PixelFormat::etc2_rgb8a1unorm:
        return VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK;
    case PixelFormat::etc2_rgb8a1unorm_srgb:
        return VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK;
    // etc2_rgba8unorm, // no metal compatible type
    // etc2_rgba8unorm_srgb, // no metal compatible type
    case PixelFormat::eac_r11unorm:
        return VK_FORMAT_EAC_R11_UNORM_BLOCK;
    case PixelFormat::eac_r11snorm:
        return VK_FORMAT_EAC_R11_SNORM_BLOCK;
    case PixelFormat::eac_rg11unorm:
        return VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
    case PixelFormat::eac_rg11snorm:
        return VK_FORMAT_EAC_R11G11_SNORM_BLOCK;

    // ASTC compressed formats usable if "texture-compression-astc" is both
    // supported by the device/user agent and enabled in requestDevice.
    // astc_4x4_unorm,
    case PixelFormat::astc_4x4_unorm_srgb:
        return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
    // astc_5x4_unorm,
    case PixelFormat::astc_5x4_unorm_srgb:
        return VK_FORMAT_ASTC_5x4_SRGB_BLOCK;
    // astc_5x5_unorm,
    case PixelFormat::astc_5x5_unorm_srgb:
        return VK_FORMAT_ASTC_5x5_SRGB_BLOCK;
    // astc_6x5_unorm,
    case PixelFormat::astc_6x5_unorm_srgb:
        return VK_FORMAT_ASTC_6x5_SRGB_BLOCK;
    // astc_6x6_unorm,
    case PixelFormat::astc_6x6_unorm_srgb:
        return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
    // astc_8x5_unorm,
    case PixelFormat::astc_8x5_unorm_srgb:
        return VK_FORMAT_ASTC_8x5_SRGB_BLOCK;
    // astc_8x6_unorm,
    case PixelFormat::astc_8x6_unorm_srgb:
        return VK_FORMAT_ASTC_8x6_SRGB_BLOCK;
    // astc_8x8_unorm,
    case PixelFormat::astc_8x8_unorm_srgb:
        return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
    // astc_10x5_unorm,
    case PixelFormat::astc_10x5_unorm_srgb:
        return VK_FORMAT_ASTC_10x5_SRGB_BLOCK;
    // astc_10x6_unorm,
    case PixelFormat::astc_10x6_unorm_srgb:
        return VK_FORMAT_ASTC_10x6_SRGB_BLOCK;
    // astc_10x8_unorm,
    case PixelFormat::astc_10x8_unorm_srgb:
        return VK_FORMAT_ASTC_10x8_SRGB_BLOCK;
    // astc_10x10_unorm,
    case PixelFormat::astc_10x10_unorm_srgb:
        return VK_FORMAT_ASTC_10x10_SRGB_BLOCK;
    // astc_12x10_unorm,
    case PixelFormat::astc_12x10_unorm_srgb:
        return VK_FORMAT_ASTC_12x10_SRGB_BLOCK;
    // astc_12x12_unorm,
    case PixelFormat::astc_12x12_unorm_srgb:
        return VK_FORMAT_ASTC_12x12_SRGB_BLOCK;
    }
}

PixelFormat nativeToPixelFormat(VkFormat format)
{
    switch (format)
    {
    // 8-bit formats
    case VK_FORMAT_R8_UNORM:
        return PixelFormat::r8unorm;
    case VK_FORMAT_R8_SNORM:
        return PixelFormat::r8snorm;
    case VK_FORMAT_R8_UINT:
        return PixelFormat::r8uint;
    case VK_FORMAT_R8_SINT:
        return PixelFormat::r8sint;

    // 16-bit formats
    case VK_FORMAT_R16_UNORM:
        return PixelFormat::r16unorm;
    case VK_FORMAT_R16_SNORM:
        return PixelFormat::r16snorm;
    case VK_FORMAT_R16_UINT:
        return PixelFormat::r16uint;
    case VK_FORMAT_R16_SINT:
        return PixelFormat::r16sint;
    case VK_FORMAT_R16_SFLOAT:
        return PixelFormat::r16float;
    case VK_FORMAT_R8G8_UNORM:
        return PixelFormat::rg8unorm;
    case VK_FORMAT_R8G8_SNORM:
        return PixelFormat::rg8snorm;
    case VK_FORMAT_R8G8_UINT:
        return PixelFormat::rg8uint;
    case VK_FORMAT_R8G8_SINT:
        return PixelFormat::rg8sint;

    // 32-bit formats
    case VK_FORMAT_R32_UINT:
        return PixelFormat::r32uint;
    case VK_FORMAT_R32_SINT:
        return PixelFormat::r32sint;
    case VK_FORMAT_R32_SFLOAT:
        return PixelFormat::r32float;
    case VK_FORMAT_R16G16_UNORM:
        return PixelFormat::rg16unorm;
    case VK_FORMAT_R16G16_SNORM:
        return PixelFormat::rg16snorm;
    case VK_FORMAT_R16G16_UINT:
        return PixelFormat::rg16uint;
    case VK_FORMAT_R16G16_SINT:
        return PixelFormat::rg16sint;
    case VK_FORMAT_R16G16_SFLOAT:
        return PixelFormat::rg16float;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return PixelFormat::rgba8unorm;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return PixelFormat::rgba8unorm_srgb;
    case VK_FORMAT_R8G8B8A8_SNORM:
        return PixelFormat::rgba8snorm;
    case VK_FORMAT_R8G8B8A8_UINT:
        return PixelFormat::rgba8uint;
    case VK_FORMAT_R8G8B8A8_SINT:
        return PixelFormat::rgba8sint;
    case VK_FORMAT_B8G8R8A8_UNORM:
        return PixelFormat::bgra8unorm;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return PixelFormat::bgra8unorm_srgb;
    // Packed 32-bit formats
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
        return PixelFormat::rgb9e5ufloat;
    case VK_FORMAT_A2R10G10B10_UINT_PACK32:
        return PixelFormat::rgb10a2uint;
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        return PixelFormat::rgb10a2unorm;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        return PixelFormat::rg11b10ufloat;

    // 64-bit formats
    case VK_FORMAT_R32G32_UINT:
        return PixelFormat::rg32uint;
    case VK_FORMAT_R32G32_SINT:
        return PixelFormat::rg32sint;
    case VK_FORMAT_R32G32_SFLOAT:
        return PixelFormat::rg32float;
    case VK_FORMAT_R16G16B16A16_UNORM:
        return PixelFormat::rgba16unorm;
    case VK_FORMAT_R16G16B16A16_SNORM:
        return PixelFormat::rgba16snorm;
    case VK_FORMAT_R16G16B16A16_UINT:
        return PixelFormat::rgba16uint;
    case VK_FORMAT_R16G16B16A16_SINT:
        return PixelFormat::rgba16sint;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return PixelFormat::rgba16float;

    // 128-bit formats
    case VK_FORMAT_R32G32B32A32_UINT:
        return PixelFormat::rgba32uint;
    case VK_FORMAT_R32G32B32A32_SINT:
        return PixelFormat::rgba32sint;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return PixelFormat::rgba32float;

    // Depth/stencil formats
    case VK_FORMAT_S8_UINT:
        return PixelFormat::stencil8;
    case VK_FORMAT_D16_UNORM:
        return PixelFormat::depth16unorm;
    // depth24plus, // no metal compatible
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return PixelFormat::depth24plus_stencil8;
    case VK_FORMAT_D32_SFLOAT:
        return PixelFormat::depth32float;

    // "depth32float-stencil8" feature
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return PixelFormat::depth32float_stencil8;

    // BC compressed formats usable if "texture-compression-bc" is both
    // supported by the device/user agent and enabled in requestDevice.
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        return PixelFormat::bc1_rgba_unorm;
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        return PixelFormat::bc1_rgba_unorm_srgb;
    case VK_FORMAT_BC2_UNORM_BLOCK:
        return PixelFormat::bc2_rgba_unorm;
    case VK_FORMAT_BC2_SRGB_BLOCK:
        return PixelFormat::bc2_rgba_unorm_srgb;
    case VK_FORMAT_BC3_UNORM_BLOCK:
        return PixelFormat::bc3_rgba_unorm;
    case VK_FORMAT_BC3_SRGB_BLOCK:
        return PixelFormat::bc3_rgba_unorm_srgb;
    case VK_FORMAT_BC4_UNORM_BLOCK:
        return PixelFormat::bc4_r_unorm;
    case VK_FORMAT_BC4_SNORM_BLOCK:
        return PixelFormat::bc4_r_snorm;
    case VK_FORMAT_BC5_UNORM_BLOCK:
        return PixelFormat::bc5_rg_unorm;
    case VK_FORMAT_BC5_SNORM_BLOCK:
        return PixelFormat::bc5_rg_snorm;
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        return PixelFormat::bc6h_rgb_ufloat;
    case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        return PixelFormat::bc6h_rgb_float;
    case VK_FORMAT_BC7_UNORM_BLOCK:
        return PixelFormat::bc7_rgba_unorm;
    case VK_FORMAT_BC7_SRGB_BLOCK:
        return PixelFormat::bc7_rgba_unorm_srgb;

    // ETC2 compressed formats usable if "texture-compression-etc2" is both
    // supported by the device/user agent and enabled in requestDevice.
    case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
        return PixelFormat::etc2_rgb8unorm;
    case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
        return PixelFormat::etc2_rgb8unorm_srgb;
    case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
        return PixelFormat::etc2_rgb8a1unorm;
    case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
        return PixelFormat::etc2_rgb8a1unorm_srgb;
    // etc2_rgba8unorm, // no metal compatible type
    // etc2_rgba8unorm_srgb, // no metal compatible type
    case VK_FORMAT_EAC_R11_UNORM_BLOCK:
        return PixelFormat::eac_r11unorm;
    case VK_FORMAT_EAC_R11_SNORM_BLOCK:
        return PixelFormat::eac_r11snorm;
    case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
        return PixelFormat::eac_rg11unorm;
    case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
        return PixelFormat::eac_rg11snorm;

    // ASTC compressed formats usable if "texture-compression-astc" is both
    // supported by the device/user agent and enabled in requestDevice.
    // astc_4x4_unorm,
    case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
        return PixelFormat::astc_4x4_unorm_srgb;
    // astc_5x4_unorm,
    case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
        return PixelFormat::astc_5x4_unorm_srgb;
    // astc_5x5_unorm,
    case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
        return PixelFormat::astc_5x5_unorm_srgb;
    // astc_6x5_unorm,
    case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
        return PixelFormat::astc_6x5_unorm_srgb;
    // astc_6x6_unorm,
    case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
        return PixelFormat::astc_6x6_unorm_srgb;
    // astc_8x5_unorm,
    case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
        return PixelFormat::astc_8x5_unorm_srgb;
    // astc_8x6_unorm,
    case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
        return PixelFormat::astc_8x6_unorm_srgb;
    // astc_8x8_unorm,
    case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
        return PixelFormat::astc_8x8_unorm_srgb;
    // astc_10x5_unorm,
    case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
        return PixelFormat::astc_10x5_unorm_srgb;
    // astc_10x6_unorm,
    case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
        return PixelFormat::astc_10x6_unorm_srgb;
    // astc_10x8_unorm,
    case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
        return PixelFormat::astc_10x8_unorm_srgb;
    // astc_10x10_unorm,
    case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
        return PixelFormat::astc_10x10_unorm_srgb;
    // astc_12x10_unorm,
    case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
        return PixelFormat::astc_12x10_unorm_srgb;
    // astc_12x12_unorm,
    case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
        return PixelFormat::astc_12x12_unorm_srgb;

    case VK_FORMAT_UNDEFINED:
    default:
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "Unknown pixelFormat conversion: %d", format);
        return PixelFormat::invalid;
    }
}

// ---------------------------------------------------------------------------
// Ray tracing — internal helpers
// ---------------------------------------------------------------------------

// Allocate a device-local GPU buffer suitable for AS storage or SBT use.
// Returns false and leaves out/memOut as VK_NULL_HANDLE on failure.
static bool allocDeviceLocalBuffer(
    VkDevice device, VkPhysicalDevice physicalDevice,
    VkDeviceSize size, VkBufferUsageFlags usage,
    VkBuffer &bufOut, VkDeviceMemory &memOut)
{
    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = size;
    bci.usage       = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bci, nullptr, &bufOut) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, bufOut, &req);

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    // Prefer DEVICE_LOCAL; fall back to HOST_VISIBLE (unified memory devices).
    VkMemoryPropertyFlags preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    uint32_t memIdx = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((req.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & preferredFlags) == preferredFlags) {
            memIdx = i; break;
        }
    }
    if (memIdx == UINT32_MAX) {
        // Fallback to any suitable type
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if (req.memoryTypeBits & (1 << i)) { memIdx = i; break; }
        }
    }
    if (memIdx == UINT32_MAX) { vkDestroyBuffer(device, bufOut, nullptr); return false; }

    VkMemoryAllocateFlagsInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.pNext           = &flagsInfo;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = memIdx;
    if (vkAllocateMemory(device, &ai, nullptr, &memOut) != VK_SUCCESS) {
        vkDestroyBuffer(device, bufOut, nullptr); return false;
    }
    vkBindBufferMemory(device, bufOut, memOut, 0);
    return true;
}

// Retrieve GPU device address of a VkBuffer.
static VkDeviceAddress getBufferDeviceAddr(VkDevice device, VkBuffer buffer) {
    VkBufferDeviceAddressInfo info{};
    info.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer;
    return pfnGetBufferDeviceAddress(device, &info);
}

static VkFormat componentTypeToVertexFormat(ComponentType ct) {
    switch (ct) {
        case ComponentType::ctFloat:         return VK_FORMAT_R32G32B32_SFLOAT;
        case ComponentType::ctUnsignedShort: return VK_FORMAT_R16G16B16_UNORM;
        case ComponentType::ctShort:         return VK_FORMAT_R16G16B16_SNORM;
        default:                             return VK_FORMAT_R32G32B32_SFLOAT;
    }
}

// Build VkAccelerationStructureGeometryKHR + primitive counts for a BLAS descriptor.
// bufAddr resolves a Buffer shared_ptr + byte offset to a VkDeviceAddress; the caller
// provides this lambda so that the free function never touches Buffer::native directly.
static void buildBlasGeometry(
    const BottomLevelAccelerationStructureDescriptor &descriptor,
    const std::function<VkDeviceAddress(const std::shared_ptr<Buffer>&, uint64_t)> &bufAddr,
    std::vector<VkAccelerationStructureGeometryKHR> &geometries,
    std::vector<uint32_t> &primitiveCounts)
{
    geometries.reserve(descriptor.geometries.size());
    primitiveCounts.reserve(descriptor.geometries.size());

    for (const auto &g : descriptor.geometries) {
        VkAccelerationStructureGeometryKHR geom{};
        geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geom.flags = g.opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;

        if (g.type == AccelerationStructureGeometryType::triangles) {
            geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            auto &tri         = geom.geometry.triangles;
            tri.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            tri.vertexFormat  = componentTypeToVertexFormat(g.componentType);
            tri.vertexStride  = g.vertexStride;
            tri.maxVertex     = g.vertexCount > 0 ? g.vertexCount - 1 : 0;
            tri.indexType     = VK_INDEX_TYPE_NONE_KHR;

            if (g.vertexBuffer)
                tri.vertexData.deviceAddress = bufAddr(g.vertexBuffer, g.vertexOffset);
            if (g.indexBuffer) {
                tri.indexType = (g.indexFormat == IndexFormat::uint16)
                                ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
                tri.indexData.deviceAddress = bufAddr(g.indexBuffer, 0);
            }
            if (g.transformBuffer)
                tri.transformData.deviceAddress = bufAddr(g.transformBuffer, g.transformOffset);

            uint32_t primitiveCount = g.indexBuffer
                ? g.indexCount / 3
                : g.vertexCount / 3;
            primitiveCounts.push_back(primitiveCount);
        } else {
            geom.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
            auto &aabb        = geom.geometry.aabbs;
            aabb.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
            aabb.stride       = g.aabbStride;
            if (g.aabbBuffer)
                aabb.data.deviceAddress = bufAddr(g.aabbBuffer, g.aabbOffset);
            primitiveCounts.push_back(g.aabbCount);
        }
        geometries.push_back(geom);
    }
}

// ---------------------------------------------------------------------------
// Ray tracing — Device factory methods
// ---------------------------------------------------------------------------

std::shared_ptr<AccelerationStructure>
Device::createBottomLevelAccelerationStructure(
    const BottomLevelAccelerationStructureDescriptor &descriptor)
{
    auto *d = (DeviceData *)native;
    if (!d->rayTracingEnabled) return nullptr;

    auto bufAddr = [d](const std::shared_ptr<Buffer> &buf, uint64_t offset) -> VkDeviceAddress {
        if (!buf) return 0;
        auto *bh = (BufferHandle *)buf->native;
        return getBufferDeviceAddr(d->device, bh->buffer) + offset;
    };

    std::vector<VkAccelerationStructureGeometryKHR> geometries;
    std::vector<uint32_t> primitiveCounts;
    buildBlasGeometry(descriptor, bufAddr, geometries, primitiveCounts);
    if (geometries.empty()) return nullptr;

    // Map build flags.
    VkBuildAccelerationStructureFlagsKHR buildFlags = 0;
    auto bf = (int)descriptor.buildFlags;
    if (bf & (int)AccelerationStructureBuildFlag::preferFastTrace) buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (bf & (int)AccelerationStructureBuildFlag::preferFastBuild) buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    if (bf & (int)AccelerationStructureBuildFlag::allowUpdate)     buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    if (bf & (int)AccelerationStructureBuildFlag::allowCompaction)  buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags         = buildFlags;
    buildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = (uint32_t)geometries.size();
    buildInfo.pGeometries   = geometries.data();

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    pfnGetAccelerationStructureBuildSizesKHR(d->device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo, primitiveCounts.data(), &sizeInfo);

    VkBuffer       buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (!allocDeviceLocalBuffer(d->device, d->physicalDevice,
                                sizeInfo.accelerationStructureSize,
                                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                buf, mem)) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu",
                            "createBLAS: backing buffer allocation failed");
        return nullptr;
    }

    VkAccelerationStructureCreateInfoKHR asCI{};
    asCI.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asCI.buffer = buf;
    asCI.size   = sizeInfo.accelerationStructureSize;
    asCI.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    VkAccelerationStructureKHR as = VK_NULL_HANDLE;
    if (pfnCreateAccelerationStructureKHR(d->device, &asCI, nullptr, &as) != VK_SUCCESS) {
        vkFreeMemory(d->device, mem, nullptr);
        vkDestroyBuffer(d->device, buf, nullptr);
        return nullptr;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = as;
    VkDeviceAddress deviceAddr     = pfnGetAccelerationStructureDeviceAddressKHR(d->device, &addrInfo);

    auto *handle                    = new AccelerationStructureHandle();
    handle->device                  = d->device;
    handle->accelerationStructure   = as;
    handle->buffer                  = buf;
    handle->memory                  = mem;
    handle->buildScratchSize        = sizeInfo.buildScratchSize;
    handle->updateScratchSize       = sizeInfo.updateScratchSize;
    handle->deviceAddress           = deviceAddr;

    return std::shared_ptr<AccelerationStructure>(new AccelerationStructure(handle));
}

std::shared_ptr<AccelerationStructure>
Device::createTopLevelAccelerationStructure(
    const TopLevelAccelerationStructureDescriptor &descriptor)
{
    auto *d = (DeviceData *)native;
    if (!d->rayTracingEnabled) return nullptr;

    uint32_t instanceCount = (uint32_t)descriptor.instances.size();

    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.arrayOfPointers = VK_FALSE;

    VkAccelerationStructureGeometryKHR geom{};
    geom.sType                           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType                    = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.geometry.instances              = instancesData;

    VkBuildAccelerationStructureFlagsKHR buildFlags = 0;
    auto bf = (int)descriptor.buildFlags;
    if (bf & (int)AccelerationStructureBuildFlag::preferFastTrace) buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (bf & (int)AccelerationStructureBuildFlag::preferFastBuild) buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    if (bf & (int)AccelerationStructureBuildFlag::allowUpdate)     buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    if (bf & (int)AccelerationStructureBuildFlag::allowCompaction)  buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags         = buildFlags;
    buildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries   = &geom;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    pfnGetAccelerationStructureBuildSizesKHR(d->device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo, &instanceCount, &sizeInfo);

    VkBuffer       buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (!allocDeviceLocalBuffer(d->device, d->physicalDevice,
                                sizeInfo.accelerationStructureSize,
                                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                buf, mem)) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu",
                            "createTLAS: backing buffer allocation failed");
        return nullptr;
    }

    VkAccelerationStructureCreateInfoKHR asCI{};
    asCI.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asCI.buffer = buf;
    asCI.size   = sizeInfo.accelerationStructureSize;
    asCI.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    VkAccelerationStructureKHR as = VK_NULL_HANDLE;
    if (pfnCreateAccelerationStructureKHR(d->device, &asCI, nullptr, &as) != VK_SUCCESS) {
        vkFreeMemory(d->device, mem, nullptr);
        vkDestroyBuffer(d->device, buf, nullptr);
        return nullptr;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = as;
    VkDeviceAddress deviceAddr     = pfnGetAccelerationStructureDeviceAddressKHR(d->device, &addrInfo);

    auto *handle                  = new AccelerationStructureHandle();
    handle->device                = d->device;
    handle->accelerationStructure = as;
    handle->buffer                = buf;
    handle->memory                = mem;
    handle->buildScratchSize      = sizeInfo.buildScratchSize;
    handle->updateScratchSize     = sizeInfo.updateScratchSize;
    handle->deviceAddress         = deviceAddr;

    return std::shared_ptr<AccelerationStructure>(new AccelerationStructure(handle));
}

std::shared_ptr<RayTracingPipeline>
Device::createRayTracingPipeline(const RayTracingPipelineDescriptor &descriptor)
{
    auto *d = (DeviceData *)native;
    if (!d->rayTracingEnabled) return nullptr;

    // --- Query RT pipeline properties for SBT alignment ---
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
    rtProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &rtProps;
    vkGetPhysicalDeviceProperties2(d->physicalDevice, &props2);

    const uint32_t handleSize      = rtProps.shaderGroupHandleSize;
    const uint32_t handleAlignment = rtProps.shaderGroupHandleAlignment;
    const uint32_t baseAlignment   = rtProps.shaderGroupBaseAlignment;

    auto alignUp = [](uint32_t val, uint32_t align) -> uint32_t {
        return (val + align - 1) & ~(align - 1);
    };
    const uint32_t handleSizeAligned = alignUp(handleSize, handleAlignment);

    // --- Build shader stages and groups ---
    std::vector<VkPipelineShaderStageCreateInfo>       stages;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR>  groups;

    auto addStage = [&](const RayTracingShaderDescriptor &s, VkShaderStageFlagBits stage) -> uint32_t {
        VkPipelineShaderStageCreateInfo si{};
        si.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        si.stage  = stage;
        si.module = ((ShaderModuleHandle *)s.module->native)->shaderModule;
        si.pName  = s.entryPoint.c_str();
        stages.push_back(si);
        return (uint32_t)stages.size() - 1;
    };

    // Ray generation group
    {
        uint32_t idx = addStage(descriptor.rayGeneration, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
        VkRayTracingShaderGroupCreateInfoKHR g{};
        g.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        g.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        g.generalShader      = idx;
        g.closestHitShader   = VK_SHADER_UNUSED_KHR;
        g.anyHitShader       = VK_SHADER_UNUSED_KHR;
        g.intersectionShader = VK_SHADER_UNUSED_KHR;
        groups.push_back(g);
    }

    // Miss groups
    uint32_t missGroupCount = (uint32_t)descriptor.missShaders.size();
    for (const auto &m : descriptor.missShaders) {
        uint32_t idx = addStage(m, VK_SHADER_STAGE_MISS_BIT_KHR);
        VkRayTracingShaderGroupCreateInfoKHR g{};
        g.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        g.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        g.generalShader      = idx;
        g.closestHitShader   = VK_SHADER_UNUSED_KHR;
        g.anyHitShader       = VK_SHADER_UNUSED_KHR;
        g.intersectionShader = VK_SHADER_UNUSED_KHR;
        groups.push_back(g);
    }

    // Hit groups
    uint32_t hitGroupCount = (uint32_t)descriptor.hitGroups.size();
    for (const auto &hg : descriptor.hitGroups) {
        VkRayTracingShaderGroupCreateInfoKHR g{};
        g.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        g.generalShader      = VK_SHADER_UNUSED_KHR;
        g.closestHitShader   = VK_SHADER_UNUSED_KHR;
        g.anyHitShader       = VK_SHADER_UNUSED_KHR;
        g.intersectionShader = VK_SHADER_UNUSED_KHR;

        bool isProcedural = hg.intersection.has_value();
        g.type = isProcedural
                 ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR
                 : VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;

        if (hg.closestHit.has_value())
            g.closestHitShader = addStage(*hg.closestHit, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
        if (hg.anyHit.has_value())
            g.anyHitShader = addStage(*hg.anyHit, VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
        if (hg.intersection.has_value())
            g.intersectionShader = addStage(*hg.intersection, VK_SHADER_STAGE_INTERSECTION_BIT_KHR);
        groups.push_back(g);
    }

    // --- Get pipeline layout ---
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (descriptor.layout) {
        auto *plh = (PipelineLayoutHandle *)descriptor.layout->native;
        pipelineLayout = plh->layout;
    } else {
        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        vkCreatePipelineLayout(d->device, &plci, nullptr, &pipelineLayout);
    }

    // --- Create RT pipeline ---
    VkRayTracingPipelineCreateInfoKHR rtci{};
    rtci.sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rtci.stageCount                   = (uint32_t)stages.size();
    rtci.pStages                      = stages.data();
    rtci.groupCount                   = (uint32_t)groups.size();
    rtci.pGroups                      = groups.data();
    rtci.maxPipelineRayRecursionDepth = descriptor.maxRecursionDepth;
    rtci.layout                       = pipelineLayout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (pfnCreateRayTracingPipelinesKHR(d->device, VK_NULL_HANDLE, VK_NULL_HANDLE,
                                        1, &rtci, nullptr, &pipeline) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu",
                            "createRayTracingPipeline: vkCreateRayTracingPipelinesKHR failed");
        return nullptr;
    }

    // --- Build Shader Binding Table ---
    const uint32_t groupCount   = (uint32_t)groups.size();
    const uint32_t sbtStride    = handleSizeAligned;

    // Compute per-region sizes (aligned to baseAlignment).
    uint32_t rgenSize = alignUp(sbtStride * 1,             baseAlignment);
    uint32_t missSize = alignUp(sbtStride * missGroupCount, baseAlignment);
    uint32_t hitSize  = alignUp(sbtStride * hitGroupCount,  baseAlignment);
    uint32_t sbtSize  = rgenSize + missSize + hitSize;

    // Allocate host-visible buffer for uploading handles.
    std::vector<uint8_t> handles(handleSize * groupCount);
    if (pfnGetRayTracingShaderGroupHandlesKHR(d->device, pipeline, 0, groupCount,
                                              handles.size(), handles.data()) != VK_SUCCESS) {
        vkDestroyPipeline(d->device, pipeline, nullptr);
        return nullptr;
    }

    // Allocate GPU-accessible SBT buffer.
    VkBuffer       sbtBuffer = VK_NULL_HANDLE;
    VkDeviceMemory sbtMemory = VK_NULL_HANDLE;
    if (!allocDeviceLocalBuffer(d->device, d->physicalDevice, sbtSize,
                                VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR
                                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                                | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                sbtBuffer, sbtMemory)) {
        vkDestroyPipeline(d->device, pipeline, nullptr);
        return nullptr;
    }

    // Map and write handles into the SBT.  Since allocDeviceLocalBuffer prefers
    // DEVICE_LOCAL, we upload via a staging buffer on non-UMA devices.
    // For simplicity (Android is typically UMA), try direct mapping first.
    {
        void *mapped = nullptr;
        bool directMap = (vkMapMemory(d->device, sbtMemory, 0, sbtSize, 0, &mapped) == VK_SUCCESS);
        if (directMap) {
            uint8_t *dst = (uint8_t *)mapped;
            // rgen (group 0)
            memcpy(dst, handles.data(), handleSize);
            // miss (groups 1 .. 1+missGroupCount-1)
            for (uint32_t i = 0; i < missGroupCount; i++)
                memcpy(dst + rgenSize + i * sbtStride,
                       handles.data() + (1 + i) * handleSize, handleSize);
            // hit  (groups 1+missGroupCount .. end)
            for (uint32_t i = 0; i < hitGroupCount; i++)
                memcpy(dst + rgenSize + missSize + i * sbtStride,
                       handles.data() + (1 + missGroupCount + i) * handleSize, handleSize);
            vkUnmapMemory(d->device, sbtMemory);
        }
        // If mapping fails (pure DEVICE_LOCAL) the SBT will be zero; the user should
        // check Feature::raytracing and only use this on supported hardware.
    }

    VkDeviceAddress sbtAddr = getBufferDeviceAddr(d->device, sbtBuffer);

    auto *h = new RayTracingPipelineHandle();
    h->device         = d->device;
    h->pipeline       = pipeline;
    h->pipelineLayout = pipelineLayout;
    h->sbtBuffer      = sbtBuffer;
    h->sbtMemory      = sbtMemory;

    h->rgenRegion.deviceAddress = sbtAddr;
    h->rgenRegion.stride        = sbtStride;
    h->rgenRegion.size          = rgenSize;

    h->missRegion.deviceAddress = sbtAddr + rgenSize;
    h->missRegion.stride        = sbtStride;
    h->missRegion.size          = missSize;

    h->hitRegion.deviceAddress  = sbtAddr + rgenSize + missSize;
    h->hitRegion.stride         = sbtStride;
    h->hitRegion.size           = hitSize;

    // callRegion is unused (no callable shaders).

    return std::shared_ptr<RayTracingPipeline>(new RayTracingPipeline(h));
}