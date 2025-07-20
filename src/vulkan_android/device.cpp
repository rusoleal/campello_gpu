#include <algorithm>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <android/log.h>
#include "campello_gpu_config.h"
#include <campello_gpu/device.hpp>
#include <campello_gpu/device_def.hpp>
#include <campello_gpu/pixel_format.hpp>
#include <campello_gpu/shader_module.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include "buffer_handle.hpp"
#include "texture_handle.hpp"
#include "shader_module_handle.hpp"
#include "render_pipeline_handle.hpp"
#include "common.hpp"

using namespace systems::leal::campello_gpu;

VkInstance instance = nullptr;

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
    appInfo.pEngineName = "campello_cpu";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;
    appInfo.pNext = nullptr;

    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "Setting required extensions");
    std::vector<const char *> requiredExtensions;
    requiredExtensions.push_back("VK_KHR_surface");
    requiredExtensions.push_back("VK_KHR_android_surface");
    // requiredExtensions.push_back("VK_KHR_get_surface_capabilities2");

    VkInstanceCreateInfo instanceInfo;
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pNext = nullptr;
    instanceInfo.flags = 0;
    instanceInfo.enabledExtensionCount = requiredExtensions.size();
    instanceInfo.ppEnabledExtensionNames = &requiredExtensions[0];
    instanceInfo.enabledLayerCount = 0;
    instanceInfo.ppEnabledLayerNames = nullptr;
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

    auto devicesDef = getDevicesDef();
    if (devicesDef.empty())
    {
        return nullptr;
    }

    return createDevice(devicesDef[0], pd);
}

std::vector<std::shared_ptr<DeviceDef>> Device::getDevicesDef()
{

    std::vector<std::shared_ptr<DeviceDef>> toReturn;

    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(getInstance(), &gpuCount, nullptr);

    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "gpuCount=%d", gpuCount);

    // Allocate memory for the GPU handles
    std::vector<VkPhysicalDevice> gpus(gpuCount);

    // Second call: Get the GPU handles
    vkEnumeratePhysicalDevices(getInstance(), &gpuCount, gpus.data());

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

        auto deviceDef = std::shared_ptr<DeviceDef>(new DeviceDef());
        deviceDef->native = (void *)a; // index on physicalDevice list
        toReturn.push_back(deviceDef);
    }

    return toReturn;
}

std::shared_ptr<Device> Device::createDevice(std::shared_ptr<DeviceDef> deviceDef, void *pd)
{

    if (deviceDef == nullptr)
    {
        return nullptr;
    }

    auto window = (ANativeWindow *)pd;

    auto deviceIndex = (uint64_t)deviceDef->native;

    // check if deviceIndex is in range;
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(getInstance(), &gpuCount, nullptr);
    if (deviceIndex > gpuCount)
    {
        return nullptr;
    }

    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(getInstance(), &gpuCount, gpus.data());

    auto gpu = gpus[deviceIndex];

    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extensionCount, availableExtensions.data());

    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "%d device extensions supported,", extensionCount);
    for (int a = 0; a < extensionCount; a++)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", " - %s v%d", availableExtensions[a].extensionName, availableExtensions[a].specVersion);
    }

    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "pepe1");
    const VkAndroidSurfaceCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .window = window};

    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "pepe2");
    VkSurfaceKHR surface;
    if (vkCreateAndroidSurfaceKHR(getInstance(), &create_info, nullptr, &surface) != VK_SUCCESS)
    {
        return nullptr;
    }

    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "pepe3");
    VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            gpu,
            surface,
            &surfaceCapabilities) != VK_SUCCESS)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkGetPhysicalDeviceSurfaceCapabilitiesKHR error");
        return nullptr;
    }

    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "surface capabilities:");
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "  minImageCount: %d", surfaceCapabilities.minImageCount);
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "  maxImageCount: %d", surfaceCapabilities.maxImageCount);
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "  currentExtent: [%d, %d]", surfaceCapabilities.currentExtent.width, surfaceCapabilities.currentExtent.height);
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "  minImageExtent: [%d, %d]", surfaceCapabilities.minImageExtent.width, surfaceCapabilities.minImageExtent.height);
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "  maxImageExtent: [%d, %d]", surfaceCapabilities.maxImageExtent.width, surfaceCapabilities.maxImageExtent.height);
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "  maxImageArrayLayers: %d", surfaceCapabilities.maxImageArrayLayers);

    uint32_t surfaceFormatCount;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &surfaceFormatCount, nullptr) != VK_SUCCESS)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkGetPhysicalDeviceSurfaceFormatsKHR error");
        return nullptr;
    }
    std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &surfaceFormatCount, surfaceFormats.data());
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "surface formats:");
    for (int a = 0; a < surfaceFormatCount; a++)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "  %s %d", pixelFormatToString(nativeToPixelFormat(surfaceFormats[a].format)).c_str(), surfaceFormats[a].colorSpace);
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
        VkBool32 pSupported;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(gpu, a, surface, &pSupported) == VK_SUCCESS)
        {
            if (pSupported)
            {
                queueFamilyIndex = a;
                break;
            }
        }
    }
    if (queueFamilyIndex == -1)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "queueFamilyIndex = -1");
        return nullptr;
    }
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "queueFamilyIndex = %d", queueFamilyIndex);

    std::vector<const char *> deviceExtensions;
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    float priority = 1.0;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = deviceExtensions.size();
    createInfo.ppEnabledExtensionNames = &deviceExtensions[0];

    VkDevice toReturn;
    if (vkCreateDevice(gpu, &createInfo, nullptr, &toReturn) != VK_SUCCESS)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateDevice() error");
        return nullptr;
    }

    // create swapchain
    VkSwapchainCreateInfoKHR swapchainData = {};
    swapchainData.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainData.pNext = nullptr;
    swapchainData.flags = 0;
    swapchainData.surface = surface;
    swapchainData.minImageCount = std::min(std::max((int)surfaceCapabilities.minImageCount, 3), (int)surfaceCapabilities.maxImageCount);
    swapchainData.imageFormat = surfaceFormats[0].format;         // TODO
    swapchainData.imageColorSpace = surfaceFormats[0].colorSpace; // TODO
    swapchainData.imageExtent = surfaceCapabilities.currentExtent;
    swapchainData.imageArrayLayers = 1;
    swapchainData.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainData.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainData.preTransform = surfaceCapabilities.currentTransform;
    swapchainData.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainData.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainData.clipped = true;
    swapchainData.oldSwapchain = 0;

    VkSwapchainKHR swapchain;
    if (vkCreateSwapchainKHR(
            toReturn,
            &swapchainData,
            nullptr,
            &swapchain) != VK_SUCCESS)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateSwapchainKHR() error");
        return nullptr;
    }

    // get swapchain images
    uint32_t swapchainImageCount;
    if (vkGetSwapchainImagesKHR(toReturn, swapchain, &swapchainImageCount, nullptr) != VK_SUCCESS)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkGetSwapchainImagesKHR() error");
        return nullptr;
    }
    std::vector<VkImage> swapchainImages(swapchainImageCount);
    std::vector<VkImageView> swapchainImageViews;
    vkGetSwapchainImagesKHR(toReturn, swapchain, &swapchainImageCount, swapchainImages.data());
    for (int a = 0; a < swapchainImageCount; a++)
    {
        VkImageViewCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.pNext = nullptr;
        info.flags = 0;
        info.image = swapchainImages[a];
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = swapchainData.imageFormat;
        info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = 1;

        VkImageView imageView;
        vkCreateImageView(toReturn, &info, nullptr, &imageView);
        swapchainImageViews.push_back(imageView);
    }

    auto deviceData = new DeviceData();
    deviceData->device = toReturn;
    vkGetDeviceQueue(toReturn, 0, 0, &deviceData->graphicsQueue);
    deviceData->physicalDevice = gpu;
    deviceData->surface = surface;
    deviceData->swapchain = swapchain;
    deviceData->imageExtent = surfaceCapabilities.currentExtent;
    deviceData->swapchainImageViews = swapchainImageViews;
    deviceData->surfaceFormat = surfaceFormats[0];

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
    imageInfo.imageType = (VkImageType)type;
    imageInfo.format = pixelFormatToNative(pixelFormat);
    imageInfo.extent = {width, height, depth};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = (VkSampleCountFlagBits)samples;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = imageUsage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.queueFamilyIndexCount = 0;
    imageInfo.pQueueFamilyIndices = nullptr;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image;
    if (vkCreateImage(deviceData->device, &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        return nullptr;
    }

    auto toReturn = new TextureHandle();
    toReturn->device = deviceData->device;
    toReturn->buffer = buffer;
    toReturn->image = image;

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
    if ((int)usage & (int)BufferUsage::vertex)
        vkUsage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
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

    VkMemoryAllocateInfo allocInfo;
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = nullptr;
    allocInfo.allocationSize = bufferRequirements.size;
    allocInfo.memoryTypeIndex = memoryType;
    VkDeviceMemory memoryHandle;
    if (vkAllocateMemory(deviceData->device, &allocInfo, nullptr, &memoryHandle) != VK_SUCCESS)
    {
        return nullptr;
        // TODO destroy bufferHandle
    }

    BufferHandle *toReturn = new BufferHandle();
    toReturn->device = deviceData->device;
    toReturn->buffer = bufferHandle;
    toReturn->memory = memoryHandle;

    return std::shared_ptr<Buffer>(new Buffer(toReturn));
}

std::string Device::getName()
{
    return "unknown";
}

std::set<Feature> Device::getFeatures()
{

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties((VkPhysicalDevice)native, &deviceProperties);

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures((VkPhysicalDevice)native, &deviceFeatures);

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
    return "unknown";
}

std::shared_ptr<ShaderModule> Device::createShaderModule(const uint8_t *buffer, uint64_t size)
{
    auto deviceData = (DeviceData *)this->native;

    VkShaderModuleCreateInfo info;
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

            return std::shared_ptr<ShaderModule>(new ShaderModule(toReturn));
        }
        default:
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreateShaderModule() unknown error");
            return nullptr;
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

    VkPipelineDynamicStateCreateInfo dynamicState;
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.pNext = nullptr;
    dynamicState.flags = 0;
    dynamicState.dynamicStateCount = dynamicStates.size();
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pNext = nullptr;
    vertexInputInfo.flags = 0;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly;
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

    VkPipelineViewportStateCreateInfo viewportState;
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext = nullptr;
    viewportState.flags = 0;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer;
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

    VkPipelineMultisampleStateCreateInfo multisampling;
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.pNext = nullptr;
    multisampling.flags = 0;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = false;

    // TODO
    VkPipelineDepthStencilStateCreateInfo depthStencil;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = false;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.pNext = nullptr;
    colorBlending.flags = 0;
    colorBlending.logicOpEnable = false;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // uniforms
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pNext = nullptr;
    pipelineLayoutInfo.flags = 0;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    VkPipelineLayout  pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(deviceData->device,&pipelineLayoutInfo, nullptr,&pipelineLayout) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "vkCreatePipelineLayout() error");
        return nullptr;
    }

    VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo = {};
    pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipelineRenderingCreateInfo.pNext = nullptr;
    pipelineRenderingCreateInfo.colorAttachmentCount = 1;
    pipelineRenderingCreateInfo.pColorAttachmentFormats = &deviceData->surfaceFormat.format;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &pipelineRenderingCreateInfo;
    pipelineInfo.stageCount = 2,
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
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
    toReturn->device = deviceData->device;
    toReturn->pipeline = pipeline;

    return std::shared_ptr<RenderPipeline>(new RenderPipeline(toReturn));
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