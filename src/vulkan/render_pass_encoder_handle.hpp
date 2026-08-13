#pragma once

#include <memory>
#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct TextureHandle;  // Forward declaration

    struct RenderPassEncoderHandle {
        VkCommandBuffer  commandBuffer;
        VkPipelineLayout pipelineLayout   = VK_NULL_HANDLE; ///< Cached from last setPipeline call.
        // Swapchain path
        VkImage          currentSwapchainImage = VK_NULL_HANDLE;
        bool             isSwapchain           = false;
        // Offscreen path
        VkImage          offscreenImage        = VK_NULL_HANDLE;
        VkExtent2D       offscreenExtent       = {};
        // Owning texture of offscreenImage (from the view's TextureViewHandle::ownerTexture),
        // so end() can write the post-pass layout back to TextureHandle::currentLayout —
        // see that field's doc comment for why this matters for later downloads.
        TextureHandle*   offscreenTextureHandle = nullptr;
        // The specific subresource offscreenImage's view targets (from
        // TextureViewHandle::baseArrayLayer/baseMipLevel) — end() barriers this exact
        // subresource, not a hardcoded (mip 0, layer 0), so attachment views into any
        // other mip/layer (e.g. one face of a cubemap render target) get correctly
        // transitioned instead of silently left in COLOR_ATTACHMENT_OPTIMAL while later
        // reads assume SHADER_READ_ONLY_OPTIMAL.
        uint32_t         offscreenBaseArrayLayer = 0;
        uint32_t         offscreenBaseMipLevel   = 0;
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
