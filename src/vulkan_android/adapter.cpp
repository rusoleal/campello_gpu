#include <set>
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

    // Check for BC texture compression (BCn formats)
    if (vkFeatures.textureCompressionBC)
        features.insert(Feature::bcTextureCompression);

    // Check depth24+stencil8 format support
    VkFormatProperties fmtProps{};
    vkGetPhysicalDeviceFormatProperties(gpu, VK_FORMAT_D24_UNORM_S8_UINT, &fmtProps);
    if (fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        features.insert(Feature::depth24Stencil8PixelFormat);

    return features;
}
