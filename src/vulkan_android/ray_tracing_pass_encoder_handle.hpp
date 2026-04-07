#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct RayTracingPassEncoderHandle {
        VkCommandBuffer  commandBuffer         = VK_NULL_HANDLE;
        VkPipelineLayout currentPipelineLayout = VK_NULL_HANDLE;
        VkStridedDeviceAddressRegionKHR rgenRegion{};
        VkStridedDeviceAddressRegionKHR missRegion{};
        VkStridedDeviceAddressRegionKHR hitRegion{};
        VkStridedDeviceAddressRegionKHR callRegion{};
    };

}
