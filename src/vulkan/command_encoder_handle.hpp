#pragma once

#include <vector>
#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu
{
    struct DeviceData; // Forward declaration

    struct CommandEncoderHandle
    {
        VkDevice                 device;
        VkCommandPool            commandPool;
        VkCommandBuffer          commandBuffer;
        VkExtent2D               imageExtent;
        VkSwapchainKHR           swapchain;
        VkQueue                  graphicsQueue;
        VkSemaphore              imageAvailableSemaphore;
        VkSemaphore              renderFinishedSemaphore;
        std::vector<VkImage>     swapchainImages;
        std::vector<VkImageView> swapchainImageViews;
        uint32_t                 currentImageIndex; ///< Set by beginRenderPass after acquire.
        DeviceData*              deviceData = nullptr; ///< Back-pointer for updating shared state.
        // Staging resources (e.g. TLAS instance buffers) transferred to CommandBufferHandle in finish().
        std::vector<VkBuffer>       stagingBuffers;
        std::vector<VkDeviceMemory> stagingMemories;
        
        // GPU timing support
        VkPhysicalDevice         physicalDevice;
        VkQueryPool              timestampQueryPool;
        uint32_t                 timestampQueryIndex;
        float                    timestampPeriod;
    };
}
