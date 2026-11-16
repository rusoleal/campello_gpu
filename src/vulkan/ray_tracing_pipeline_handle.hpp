#pragma once

#include <memory>
#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {
    class PipelineLayout;


    struct RayTracingPipelineHandle {
        VkDevice         device          = VK_NULL_HANDLE;
        VkPipeline       pipeline        = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout  = VK_NULL_HANDLE;
        /// The caller's PipelineLayout, kept alive as long as the pipeline: the
        /// pipeline records the raw VkPipelineLayout above, and binding descriptor
        /// sets against a destroyed layout is undefined (callers commonly drop
        /// the layout right after creating the pipeline, as on Metal).
        std::shared_ptr<PipelineLayout> layoutRef;
        VkBuffer         sbtBuffer       = VK_NULL_HANDLE;
        VkDeviceMemory   sbtMemory       = VK_NULL_HANDLE;
        VkStridedDeviceAddressRegionKHR rgenRegion{};
        VkStridedDeviceAddressRegionKHR missRegion{};
        VkStridedDeviceAddressRegionKHR hitRegion{};
        VkStridedDeviceAddressRegionKHR callRegion{};
    };

}
