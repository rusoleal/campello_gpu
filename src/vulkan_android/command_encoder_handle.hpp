#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu
{
    struct CommandEncoderHandle
    {
        VkDevice device;
        VkCommandPool commandPool;
        VkCommandBuffer commandBuffer;
        VkExtent2D imageExtent;
        std::vector<VkImageView> swapchainImageViews;
    };
}
