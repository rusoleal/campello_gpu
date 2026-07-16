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

    // Detect cooperative matrix support: VK_KHR_cooperative_matrix depends on
    // VK_KHR_shader_float16_int8, which is core as of Vulkan 1.2 — so on a 1.2+
    // driver that dependency won't appear in the extension list at all.
    VkPhysicalDeviceProperties deviceProps{};
    vkGetPhysicalDeviceProperties(gpu, &deviceProps);
    bool hasCoopMat = false, hasFloat16Int8 = false;
    for (const auto &e : exts) {
        if (strcmp(e.extensionName, VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME)  == 0) hasCoopMat     = true;
        if (strcmp(e.extensionName, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME) == 0) hasFloat16Int8 = true;
    }
    const bool isVulkan12Plus = deviceProps.apiVersion >= VK_API_VERSION_1_2;
    if (hasCoopMat && (hasFloat16Int8 || isVulkan12Plus))
        features.insert(Feature::cooperativeMatrix);

    // shaderFloat16 lives behind the same extension/1.2-core dependency as
    // cooperative matrix above, but is its own independent feature bit — query
    // the actual VkPhysicalDeviceShaderFloat16Int8Features::shaderFloat16 value
    // rather than assuming presence of the extension implies it's set.
    if (hasFloat16Int8 || isVulkan12Plus) {
        VkPhysicalDeviceShaderFloat16Int8Features float16Int8Features{};
        float16Int8Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &float16Int8Features;
        vkGetPhysicalDeviceFeatures2(gpu, &features2);
        if (float16Int8Features.shaderFloat16)
            features.insert(Feature::fp16);
    }

    // Subgroup operations are core since Vulkan 1.1 (no extension/enable step
    // needed) — query VkPhysicalDeviceSubgroupProperties for the actual
    // supportedOperations bitmask rather than assuming 1.1+ implies everything.
    // Require basic + ballot + arithmetic in the compute stage: the minimum set
    // that makes "subgroup operations" actually useful for a compute kernel.
    VkPhysicalDeviceSubgroupProperties subgroupProps{};
    subgroupProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    VkPhysicalDeviceProperties2 subgroupProps2{};
    subgroupProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    subgroupProps2.pNext = &subgroupProps;
    vkGetPhysicalDeviceProperties2(gpu, &subgroupProps2);
    constexpr VkSubgroupFeatureFlags kRequiredSubgroupOps =
        VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT;
    if ((subgroupProps.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
        (subgroupProps.supportedOperations & kRequiredSubgroupOps) == kRequiredSubgroupOps)
        features.insert(Feature::subgroupOperations);

    return features;
}
