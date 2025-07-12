#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <android/log.h>
#include "campello_gpu_config.h"
#include <campello_gpu/device.hpp>
#include <campello_gpu/device_def.hpp>
#include "buffer_handle.hpp"
#include "texture_handle.hpp"
#include "common.hpp"

using namespace systems::leal::campello_gpu;

VkInstance instance = nullptr;

VkFormat fromPixelFormat(PixelFormat format);

std::string queueFlagsToString(VkQueueFlags flags) {
    std::vector<std::string> stringFlags;
    if (flags&VK_QUEUE_GRAPHICS_BIT) stringFlags.push_back("VK_QUEUE_GRAPHICS_BIT");
    if (flags&VK_QUEUE_COMPUTE_BIT) stringFlags.push_back("VK_QUEUE_COMPUTE_BIT");
    if (flags&VK_QUEUE_TRANSFER_BIT) stringFlags.push_back("VK_QUEUE_TRANSFER_BIT");
    if (flags&VK_QUEUE_SPARSE_BINDING_BIT) stringFlags.push_back("VK_QUEUE_SPARSE_BINDING_BIT");
    if (flags&VK_QUEUE_PROTECTED_BIT) stringFlags.push_back("VK_QUEUE_PROTECTED_BIT");
    if (flags&VK_QUEUE_VIDEO_DECODE_BIT_KHR) stringFlags.push_back("VK_QUEUE_VIDEO_DECODE_BIT_KHR");
    if (flags&VK_QUEUE_VIDEO_ENCODE_BIT_KHR) stringFlags.push_back("VK_QUEUE_VIDEO_ENCODE_BIT_KHR");
    if (flags&VK_QUEUE_OPTICAL_FLOW_BIT_NV) stringFlags.push_back("VK_QUEUE_OPTICAL_FLOW_BIT_NV");
    //if (flags&VK_QUEUE_DATA_GRAPH_BIT_ARM) stringFlags.push_back("VK_QUEUE_DATA_GRAPH_BIT_ARM");

    std::string toReturn = "[";
    if (!stringFlags.empty()) {
        for (int a=0; a<stringFlags.size()-1; a++) {
            toReturn += stringFlags[a]+" | ";
        }
        toReturn += stringFlags[stringFlags.size()-1];
    }
    toReturn += "]";
    return toReturn;
}

VkInstance systems::leal::campello_gpu::getInstance() {

    if (instance != nullptr) {
        return instance;
    }

    uint32_t extensionCount = 0;
    std::vector<VkExtensionProperties> extensions;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","%d instance extensions supported,", extensionCount);

    if (extensionCount > 0) {
        extensions.resize(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
        for (int a=0; a<extensionCount; a++) {
            __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu"," - %s v%d", extensions[a].extensionName, extensions[a].specVersion);
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

    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","Setting required extensions");
    std::vector<const char*> requiredExtensions;
    requiredExtensions.push_back("VK_KHR_surface");
    requiredExtensions.push_back("VK_KHR_android_surface");
    //requiredExtensions.push_back("VK_KHR_get_surface_capabilities2");

    VkInstanceCreateInfo instanceInfo;
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pNext = nullptr;
    instanceInfo.flags = 0;
    instanceInfo.enabledExtensionCount = requiredExtensions.size();
    instanceInfo.ppEnabledExtensionNames = &requiredExtensions[0];
    instanceInfo.enabledLayerCount = 0;
    instanceInfo.ppEnabledLayerNames = nullptr;
    instanceInfo.pApplicationInfo = &appInfo;

    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","Creating vulkan instance");
    VkInstance ins;
    auto res = vkCreateInstance(&instanceInfo, nullptr, &ins);
    if (res != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","vkCreateInstance failed with error: %d", res);
        return nullptr;
    }
    instance = ins;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","Vulkan instance created");
    return ins;
}

Device::Device(void *pd) {
    native = pd;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","Device::Device()");
}

Device::~Device() {
    if (native != nullptr) {
        DeviceData *deviceData = (DeviceData *)native;
        vkDestroyDevice(deviceData->device, nullptr);
        delete deviceData;
        __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","Device::~Device()");
    }
}

/*void *Device::getNative()
{
    return native;
}*/

std::shared_ptr<Device> Device::createDefaultDevice(void *pd) {

    auto devicesDef = getDevicesDef();
    if (devicesDef.empty()) {
        return nullptr;
    }

    return createDevice(devicesDef[0], pd);
}

std::vector<std::shared_ptr<DeviceDef>> Device::getDevicesDef() {

    std::vector<std::shared_ptr<DeviceDef>> toReturn;

    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(getInstance(), &gpuCount, nullptr);

    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","gpuCount=%d", gpuCount);

    // Allocate memory for the GPU handles
    std::vector<VkPhysicalDevice> gpus(gpuCount);

    // Second call: Get the GPU handles
    vkEnumeratePhysicalDevices(getInstance(), &gpuCount, gpus.data());

    // Now, 'gpus' contains a list of VkPhysicalDevice handles
    for (int a=0; a<gpuCount; a++) {
        const VkPhysicalDevice &gpu = gpus[a];

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, queueFamilies.data());

        __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","gpu[%d].queueFamilyCount = %d", a,queueFamilyCount);

        for (int b=0; b<queueFamilyCount; b++) {
            auto flags = queueFlagsToString(queueFamilies[b].queueFlags);
            __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","gpu[%d].queueuFamily[%d].queueFlags = %s", a, b, flags.c_str());
            __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","gpu[%d].queueuFamily[%d].queueCount = %d", a, b, queueFamilies[b].queueCount);
            __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","gpu[%d].queueuFamily[%d].minImageTransferGranularity.width = %d", a, b, queueFamilies[b].minImageTransferGranularity.width);
            __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","gpu[%d].queueuFamily[%d].minImageTransferGranularity.height = %d", a, b, queueFamilies[b].minImageTransferGranularity.height);
            __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","gpu[%d].queueuFamily[%d].minImageTransferGranularity.depth = %d", a, b, queueFamilies[b].minImageTransferGranularity.depth);
        }

        auto deviceDef = std::shared_ptr<DeviceDef>(new DeviceDef());
        deviceDef->native = (void *)a; // index on physicalDevice list
        toReturn.push_back(deviceDef);
    }

    return toReturn;
}

std::shared_ptr<Device> Device::createDevice(std::shared_ptr<DeviceDef> deviceDef, void *pd) {

    if (deviceDef == nullptr) {
        return nullptr;
    }

    auto window = (ANativeWindow *)pd;

    auto deviceIndex = (uint64_t)deviceDef->native;

    // check if deviceIndex is in range;
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(getInstance(), &gpuCount, nullptr);
    if (deviceIndex>gpuCount) {
        return nullptr;
    }

    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(getInstance(), &gpuCount, gpus.data());

    auto gpu = gpus[deviceIndex];

    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extensionCount, availableExtensions.data());

    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","%d device extensions supported,", extensionCount);
    for (int a=0; a<extensionCount; a++) {
        __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu"," - %s v%d", availableExtensions[a].extensionName, availableExtensions[a].specVersion);
    }

    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","pepe1");
    const VkAndroidSurfaceCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .window = window
    };

    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","pepe2");
    VkSurfaceKHR surface;
    if (vkCreateAndroidSurfaceKHR(getInstance(), &create_info, nullptr, &surface) != VK_SUCCESS) {
        return nullptr;
    }

    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","pepe3");
    VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        gpu,
        surface,
        &surfaceCapabilities
    ) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","vkGetPhysicalDeviceSurfaceCapabilitiesKHR error");
        return nullptr;
    }

    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","surface capabilities:");
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","  minImageCount: %d",surfaceCapabilities.minImageCount);
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","  maxImageCount: %d",surfaceCapabilities.maxImageCount);
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","  currentExtent: [%d, %d]",surfaceCapabilities.currentExtent.width, surfaceCapabilities.currentExtent.height);
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","  minImageExtent: [%d, %d]",surfaceCapabilities.minImageExtent.width, surfaceCapabilities.minImageExtent.height);
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","  maxImageExtent: [%d, %d]",surfaceCapabilities.maxImageExtent.width, surfaceCapabilities.maxImageExtent.height);
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","  maxImageArrayLayers: %d",surfaceCapabilities.maxImageArrayLayers);

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(gpu, &deviceFeatures);

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, queueFamilies.data());

    int queueFamilyIndex = -1;
    for (int a=0; a<queueFamilyCount; a++) {
        VkBool32 pSupported;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(gpu, a, surface, &pSupported) == VK_SUCCESS) {
            if (pSupported) {
                queueFamilyIndex = a;
                break;
            }
        }
    }
    if (queueFamilyIndex == -1) {
        __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","queueFamilyIndex = -1");
        return nullptr;
    }
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","queueFamilyIndex = %d", queueFamilyIndex);

    std::vector<const char *>deviceExtensions;
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
    if (vkCreateDevice(gpu, &createInfo, nullptr, &toReturn) != VK_SUCCESS) {
        return nullptr;
    }

    auto deviceData = new DeviceData();
    deviceData->device = toReturn;
    vkGetDeviceQueue(toReturn, 0, 0, &deviceData->graphicsQueue);
    deviceData->physicalDevice = gpu;

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
    for (int a=1; a<mipLevels; a++) {
        w = w/2;
        h = h/2;
        uint64_t mipSize = (w * h * depth * samples * getPixelFormatSize(pixelFormat)) / 8;
        bufferSize += mipSize;
    }

    // create buffer
    auto buffer = createBuffer(bufferSize, BufferUsage::copySrc);

    VkImageUsageFlags imageUsage = 0;
    if ((int)usageMode & (int)TextureUsage::copySrc) imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if ((int)usageMode & (int)TextureUsage::copyDst) imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((int)usageMode & (int)TextureUsage::renderTarget) imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if ((int)usageMode & (int)TextureUsage::storageBinding) imageUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if ((int)usageMode & (int)TextureUsage::textureBinding) imageUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;

    // create image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext = nullptr;
    imageInfo.imageType = (VkImageType)type;
    imageInfo.format = fromPixelFormat(pixelFormat);
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
    if (vkCreateImage(deviceData->device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        return nullptr;
    }

    TextureHandle *toReturn = new TextureHandle();
    toReturn->device = deviceData->device;
    toReturn->buffer = buffer;
    toReturn->image = image;

    return std::shared_ptr<Texture>(new Texture(toReturn));

}

uint32_t findMemoryType(uint32_t typeFilter, VkPhysicalDeviceMemoryProperties &memProperties, VkMemoryPropertyFlags properties) {
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

std::shared_ptr<Buffer> Device::createBuffer(uint64_t size, BufferUsage usage) {

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBufferUsageFlags vkUsage = 0;
    if ((int)usage&(int)BufferUsage::vertex) vkUsage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    auto deviceData = (DeviceData *)this->native;

    VkBuffer bufferHandle;
    if (vkCreateBuffer(deviceData->device, &bufferInfo, nullptr, &bufferHandle) != VK_SUCCESS) {
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
    if (vkAllocateMemory(deviceData->device, &allocInfo, nullptr, &memoryHandle) != VK_SUCCESS) {
        return nullptr;
        // TODO destroy bufferHandle
    }

    BufferHandle *toReturn = new BufferHandle();
    toReturn->device = deviceData->device;
    toReturn->buffer = bufferHandle;
    toReturn->memory = memoryHandle;

    return std::shared_ptr<Buffer>(new Buffer(toReturn));
}

std::string Device::getName() {
    return "unknown";
}

std::set<Feature> Device::getFeatures() {

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties((VkPhysicalDevice)native, &deviceProperties);

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures((VkPhysicalDevice)native, &deviceFeatures);

    std::set<Feature> toReturn;

    if (deviceFeatures.textureCompressionBC) {
        toReturn.insert(Feature::bcTextureCompression);
    }

    if (deviceFeatures.geometryShader) {
        toReturn.insert(Feature::geometryShader);
    }

    return toReturn;
}

std::string Device::getEngineVersion() {
    return "unknown";
}

std::string systems::leal::campello_gpu::getVersion() {
    return std::to_string(campello_gpu_VERSION_MAJOR) + "." + std::to_string(campello_gpu_VERSION_MINOR) + "." + std::to_string(campello_gpu_VERSION_PATCH);
}

VkFormat fromPixelFormat(PixelFormat format) {
    switch (format) {
        case PixelFormat::invalid: return VK_FORMAT_UNDEFINED;
        // 8-bit formats
        case PixelFormat::r8unorm: return VK_FORMAT_R8_UNORM;
        case PixelFormat::r8snorm: return VK_FORMAT_R8_SNORM;
        case PixelFormat::r8uint: return VK_FORMAT_R8_UINT;
        case PixelFormat::r8sint: return VK_FORMAT_R8_SINT;

        // 16-bit formats
        case PixelFormat::r16unorm: return VK_FORMAT_R16_UNORM;
        case PixelFormat::r16snorm: return VK_FORMAT_R16_SNORM;
        case PixelFormat::r16uint: return VK_FORMAT_R16_UINT;
        case PixelFormat::r16sint: return VK_FORMAT_R16_SINT;
        case PixelFormat::r16float: return VK_FORMAT_R16_SFLOAT;
        case PixelFormat::rg8unorm: return VK_FORMAT_R8G8_UNORM;
        case PixelFormat::rg8snorm: return VK_FORMAT_R8G8_SNORM;
        case PixelFormat::rg8uint: return VK_FORMAT_R8G8_UINT;
        case PixelFormat::rg8sint: return VK_FORMAT_R8G8_SINT;

        // 32-bit formats
        case PixelFormat::r32uint: return VK_FORMAT_R32_UINT;
        case PixelFormat::r32sint: return VK_FORMAT_R32_SINT;
        case PixelFormat::r32float: return VK_FORMAT_R32_SFLOAT;
        case PixelFormat::rg16unorm: return VK_FORMAT_R16G16_UNORM;
        case PixelFormat::rg16snorm: return VK_FORMAT_R16G16_SNORM;
        case PixelFormat::rg16uint: return VK_FORMAT_R16G16_UINT;
        case PixelFormat::rg16sint: return VK_FORMAT_R16G16_SINT;
        case PixelFormat::rg16float: return VK_FORMAT_R16G16_SFLOAT;
        case PixelFormat::rgba8unorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::rgba8unorm_srgb: return VK_FORMAT_R8G8B8A8_SRGB;
        case PixelFormat::rgba8snorm: return VK_FORMAT_R8G8B8A8_SNORM;
        case PixelFormat::rgba8uint: return VK_FORMAT_R8G8B8A8_UINT;
        case PixelFormat::rgba8sint: return VK_FORMAT_R8G8B8A8_SINT;
        case PixelFormat::bgra8unorm: return VK_FORMAT_B8G8R8A8_UNORM;
        case PixelFormat::bgra8unorm_srgb: return VK_FORMAT_B8G8R8A8_SRGB;
        // Packed 32-bit formats
        case PixelFormat::rgb9e5ufloat: return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
        case PixelFormat::rgb10a2uint: return VK_FORMAT_A2R10G10B10_UINT_PACK32;
        case PixelFormat::rgb10a2unorm: return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        case PixelFormat::rg11b10ufloat: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;

        // 64-bit formats
        case PixelFormat::rg32uint: return VK_FORMAT_R32_UINT;
        case PixelFormat::rg32sint: return VK_FORMAT_R32_SINT;
        case PixelFormat::rg32float: return VK_FORMAT_R32G32_SFLOAT;
        case PixelFormat::rgba16unorm: return VK_FORMAT_R16G16B16A16_UNORM;
        case PixelFormat::rgba16snorm: return VK_FORMAT_R16G16B16A16_SNORM;
        case PixelFormat::rgba16uint: return VK_FORMAT_R16G16B16A16_UINT;
        case PixelFormat::rgba16sint: return VK_FORMAT_R16G16B16A16_SINT;
        case PixelFormat::rgba16float: return VK_FORMAT_R16G16B16A16_SFLOAT;

        // 128-bit formats
        case PixelFormat::rgba32uint: return VK_FORMAT_R32G32B32A32_UINT;
        case PixelFormat::rgba32sint: return VK_FORMAT_R32G32B32A32_SINT;
        case PixelFormat::rgba32float: return VK_FORMAT_R32G32B32A32_SFLOAT;

        // Depth/stencil formats
        case PixelFormat::stencil8: return VK_FORMAT_S8_UINT;
        case PixelFormat::depth16unorm: return VK_FORMAT_D16_UNORM;
        // depth24plus, // no metal compatible
        case PixelFormat::depth24plus_stencil8: return VK_FORMAT_D24_UNORM_S8_UINT;
        case PixelFormat::depth32float: return VK_FORMAT_D32_SFLOAT;

        // "depth32float-stencil8" feature
        case PixelFormat::depth32float_stencil8: return VK_FORMAT_D32_SFLOAT_S8_UINT;

        // BC compressed formats usable if "texture-compression-bc" is both
        // supported by the device/user agent and enabled in requestDevice.
        case PixelFormat::bc1_rgba_unorm: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case PixelFormat::bc1_rgba_unorm_srgb: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case PixelFormat::bc2_rgba_unorm: return VK_FORMAT_BC2_UNORM_BLOCK;
        case PixelFormat::bc2_rgba_unorm_srgb: return VK_FORMAT_BC2_SRGB_BLOCK;
        case PixelFormat::bc3_rgba_unorm: return VK_FORMAT_BC3_UNORM_BLOCK;
        case PixelFormat::bc3_rgba_unorm_srgb: return VK_FORMAT_BC3_SRGB_BLOCK;
        case PixelFormat::bc4_r_unorm: return VK_FORMAT_BC4_UNORM_BLOCK;
        case PixelFormat::bc4_r_snorm: return VK_FORMAT_BC4_SNORM_BLOCK;
        case PixelFormat::bc5_rg_unorm: return VK_FORMAT_BC5_UNORM_BLOCK;
        case PixelFormat::bc5_rg_snorm: return VK_FORMAT_BC5_SNORM_BLOCK;
        case PixelFormat::bc6h_rgb_ufloat: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case PixelFormat::bc6h_rgb_float: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
        case PixelFormat::bc7_rgba_unorm: return VK_FORMAT_BC7_UNORM_BLOCK;
        case PixelFormat::bc7_rgba_unorm_srgb: return VK_FORMAT_BC7_SRGB_BLOCK;

        // ETC2 compressed formats usable if "texture-compression-etc2" is both
        // supported by the device/user agent and enabled in requestDevice.
        case PixelFormat::etc2_rgb8unorm: return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
        case PixelFormat::etc2_rgb8unorm_srgb: return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
        case PixelFormat::etc2_rgb8a1unorm: return VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK;
        case PixelFormat::etc2_rgb8a1unorm_srgb: return VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK;
        // etc2_rgba8unorm, // no metal compatible type
        // etc2_rgba8unorm_srgb, // no metal compatible type
        case PixelFormat::eac_r11unorm: return VK_FORMAT_EAC_R11_UNORM_BLOCK;
        case PixelFormat::eac_r11snorm: return VK_FORMAT_EAC_R11_SNORM_BLOCK;
        case PixelFormat::eac_rg11unorm: return VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
        case PixelFormat::eac_rg11snorm: return VK_FORMAT_EAC_R11G11_SNORM_BLOCK;

        // ASTC compressed formats usable if "texture-compression-astc" is both
        // supported by the device/user agent and enabled in requestDevice.
        // astc_4x4_unorm,
        case PixelFormat::astc_4x4_unorm_srgb: return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
        // astc_5x4_unorm,
        case PixelFormat::astc_5x4_unorm_srgb: return VK_FORMAT_ASTC_5x4_SRGB_BLOCK;
        // astc_5x5_unorm,
        case PixelFormat::astc_5x5_unorm_srgb: return VK_FORMAT_ASTC_5x5_SRGB_BLOCK;
        // astc_6x5_unorm,
        case PixelFormat::astc_6x5_unorm_srgb: return VK_FORMAT_ASTC_6x5_SRGB_BLOCK;
        // astc_6x6_unorm,
        case PixelFormat::astc_6x6_unorm_srgb: return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
        // astc_8x5_unorm,
        case PixelFormat::astc_8x5_unorm_srgb: return VK_FORMAT_ASTC_8x5_SRGB_BLOCK;
        // astc_8x6_unorm,
        case PixelFormat::astc_8x6_unorm_srgb: return VK_FORMAT_ASTC_8x6_SRGB_BLOCK;
        // astc_8x8_unorm,
        case PixelFormat::astc_8x8_unorm_srgb: return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
        // astc_10x5_unorm,
        case PixelFormat::astc_10x5_unorm_srgb: return VK_FORMAT_ASTC_10x5_SRGB_BLOCK;
        // astc_10x6_unorm,
        case PixelFormat::astc_10x6_unorm_srgb: return VK_FORMAT_ASTC_10x6_SRGB_BLOCK;
        // astc_10x8_unorm,
        case PixelFormat::astc_10x8_unorm_srgb: return VK_FORMAT_ASTC_10x8_SRGB_BLOCK;
        // astc_10x10_unorm,
        case PixelFormat::astc_10x10_unorm_srgb: return VK_FORMAT_ASTC_10x10_SRGB_BLOCK;
        // astc_12x10_unorm,
        case PixelFormat::astc_12x10_unorm_srgb: return VK_FORMAT_ASTC_12x10_SRGB_BLOCK;
        // astc_12x12_unorm,
        case PixelFormat::astc_12x12_unorm_srgb: return VK_FORMAT_ASTC_12x12_SRGB_BLOCK;
    }
}