#pragma once

#include <vector>
#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu
{
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
    };
}
