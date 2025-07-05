#include <vulkan/vulkan.h>
#include <android/log.h>
#include "campello_gpu_config.h"
#include <campello_gpu/device.hpp>
#include <campello_gpu/device_def.hpp>
#include "buffer_handle.hpp"

using namespace systems::leal::campello_gpu;

struct DeviceData {
    VkDevice device;
    VkQueue graphicsQueue;
    VkPhysicalDevice physicalDevice;
};

VkInstance *instance = nullptr;

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

VkInstance *getInstance() {

    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","%d vulkan extensions supported,", extensionCount);

    if (extensionCount > 0) {
        std::vector<VkExtensionProperties> properties(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, properties.data());
        for (int a=0; a<extensionCount; a++) {
            __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu"," - %s v%d", properties[a].extensionName, properties[a].specVersion);
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

    VkInstanceCreateInfo instanceInfo;
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pNext = nullptr;
    instanceInfo.flags = 0;
    instanceInfo.enabledExtensionCount = 0;
    instanceInfo.ppEnabledExtensionNames = nullptr;
    instanceInfo.enabledLayerCount = 0;
    instanceInfo.ppEnabledLayerNames = nullptr;
    instanceInfo.pApplicationInfo = &appInfo;

    VkInstance ins;
    auto res = vkCreateInstance(&instanceInfo, nullptr, &ins);
    if (res != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","vkCreateInstance failed with error: %d", res);
        return nullptr;
    }
    return new VkInstance(ins);
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

std::shared_ptr<Device> Device::createDefaultDevice() {

    auto devicesDef = getDevicesDef();
    if (devicesDef.empty()) {
        return nullptr;
    }

    return createDevice(devicesDef[0]);
}

std::vector<std::shared_ptr<DeviceDef>> Device::getDevicesDef() {

    if (instance == nullptr) {
        instance = getInstance();
    }

    std::vector<std::shared_ptr<DeviceDef>> toReturn;

    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(*instance, &gpuCount, nullptr);

    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","gpuCount=%d", gpuCount);

    // Allocate memory for the GPU handles
    std::vector<VkPhysicalDevice> gpus(gpuCount);

    // Second call: Get the GPU handles
    vkEnumeratePhysicalDevices(*instance, &gpuCount, gpus.data());

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

std::shared_ptr<Device> Device::createDevice(std::shared_ptr<DeviceDef> deviceDef) {

    if (deviceDef == nullptr) {
        return nullptr;
    }

    auto deviceIndex = (uint64_t)deviceDef->native;

    // check if deviceIndex is in range;
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(*instance, &gpuCount, nullptr);
    if (deviceIndex>gpuCount) {
        return nullptr;
    }

    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(*instance, &gpuCount, gpus.data());

    auto gpu = gpus[deviceIndex];

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(gpu, &deviceFeatures);

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, queueFamilies.data());

    float priority = 1.0;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = 0;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;

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
    StorageMode storageMode, 
    uint32_t width, 
    uint32_t height, 
    PixelFormat pixelFormat,
    //TextureCoordinateSystem textureCoordinateSystem,
    TextureUsage usageMode) {

    return nullptr;
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
