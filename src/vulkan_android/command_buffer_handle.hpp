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
        
        // GPU timing data
        uint64_t gpuStartTimestamp = 0;
        uint64_t gpuEndTimestamp = 0;
        bool hasTimingData = false;
        float timestampPeriod = 1.0f;  // Nanoseconds per tick
        VkQueryPool queryPool = VK_NULL_HANDLE;  // If we allocated a dedicated pool
        uint32_t queryStartIndex = 0;
        uint32_t queryEndIndex = 1;
    };

}
