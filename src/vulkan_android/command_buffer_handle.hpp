#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct CommandBufferHandle {
        VkDevice        device;
        VkCommandPool   commandPool;
        VkCommandBuffer commandBuffer;
        VkQueue         graphicsQueue;
        VkSwapchainKHR  swapchain;
        uint32_t        currentImageIndex;
        bool            hasSwapchain; ///< False if the pass did not render to the swapchain.
    };

}
