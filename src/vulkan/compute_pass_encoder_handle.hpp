#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct ComputePassEncoderHandle {
        VkCommandBuffer  commandBuffer;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE; ///< Cached from last setPipeline call.
    };

}
