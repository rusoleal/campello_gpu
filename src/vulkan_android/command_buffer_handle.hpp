#pragma once

#include <vector>
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
        // Staging buffers allocated during command recording (e.g. TLAS instance uploads).
        // Freed after the command buffer is submitted and GPU work finishes.
        std::vector<VkBuffer>       stagingBuffers;
        std::vector<VkDeviceMemory> stagingMemories;
    };

}
