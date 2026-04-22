#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct RayTracingPipelineHandle {
        VkDevice         device          = VK_NULL_HANDLE;
        VkPipeline       pipeline        = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout  = VK_NULL_HANDLE;
        VkBuffer         sbtBuffer       = VK_NULL_HANDLE;
        VkDeviceMemory   sbtMemory       = VK_NULL_HANDLE;
        VkStridedDeviceAddressRegionKHR rgenRegion{};
        VkStridedDeviceAddressRegionKHR missRegion{};
        VkStridedDeviceAddressRegionKHR hitRegion{};
        VkStridedDeviceAddressRegionKHR callRegion{};
    };

}
