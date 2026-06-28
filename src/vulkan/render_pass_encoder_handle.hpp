#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct RenderPassEncoderHandle {
        VkCommandBuffer  commandBuffer;
        VkPipelineLayout pipelineLayout   = VK_NULL_HANDLE; ///< Cached from last setPipeline call.
        // Swapchain path
        VkImage          currentSwapchainImage = VK_NULL_HANDLE;
        bool             isSwapchain           = false;
        // Offscreen path
        VkImage          offscreenImage        = VK_NULL_HANDLE;
        VkExtent2D       offscreenExtent       = {};
        // Occlusion queries
        VkQueryPool      queryPool             = VK_NULL_HANDLE;
    };

}
