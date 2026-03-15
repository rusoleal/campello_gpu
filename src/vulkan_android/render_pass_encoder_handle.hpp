#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct RenderPassEncoderHandle {
        VkCommandBuffer commandBuffer;
        VkImage         currentSwapchainImage; ///< Used for the post-render layout barrier.
    };

}
