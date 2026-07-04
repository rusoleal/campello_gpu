#pragma once

#include <memory>
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
        // Keeps the CPU-side TextureView alive for the duration of the offscreen pass.
        // vkCmdBeginRenderingKHR records the raw VkImageView; destroying the TextureView
        // before the pass ends would make the validation layer look up a freed handle.
        std::shared_ptr<void> offscreenViewRef;
        // Occlusion queries
        VkQueryPool      queryPool             = VK_NULL_HANDLE;
        // True when using traditional vkCmdBeginRenderPass / vkCmdEndRenderPass.
        bool             usesTraditionalRenderPass = false;
    };

}
