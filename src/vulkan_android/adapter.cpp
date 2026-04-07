#include <cstring>
#include <set>
#include <vector>
#include <vulkan/vulkan.h>
#include <campello_gpu/adapter.hpp>
#include <campello_gpu/constants/feature.hpp>

using namespace systems::leal::campello_gpu;

Adapter::Adapter() {
}

std::set<Feature> Adapter::getFeatures() {
    std::set<Feature> features;
    auto gpu = (VkPhysicalDevice)native;
    if (!gpu) return features;

    VkPhysicalDeviceFeatures vkFeatures{};
    vkGetPhysicalDeviceFeatures(gpu, &vkFeatures);

    if (vkFeatures.geometryShader)
        features.insert(Feature::geometryShader);

    if (vkFeatures.textureCompressionBC)
        features.insert(Feature::bcTextureCompression);

    // Check depth24+stencil8 format support
    VkFormatProperties fmtProps{};
    vkGetPhysicalDeviceFormatProperties(gpu, VK_FORMAT_D24_UNORM_S8_UINT, &fmtProps);
    if (fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        features.insert(Feature::depth24Stencil8PixelFormat);

    // Detect hardware ray tracing: requires all three KHR extensions.
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extCount, exts.data());

    bool hasAS = false, hasRTP = false, hasDHO = false;
    for (const auto &e : exts) {
        if (strcmp(e.extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)   == 0) hasAS  = true;
        if (strcmp(e.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)     == 0) hasRTP = true;
        if (strcmp(e.extensionName, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0) hasDHO = true;
    }
    if (hasAS && hasRTP && hasDHO)
        features.insert(Feature::raytracing);

    return features;
}
