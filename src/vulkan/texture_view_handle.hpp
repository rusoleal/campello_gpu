#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct TextureHandle;  // Forward declaration

    struct TextureViewHandle {
        VkDevice    device;
        VkImageView imageView;
        VkImage     image  = VK_NULL_HANDLE; ///< Backing image (for layout transitions in beginRenderPass).
        VkFormat    format = VK_FORMAT_UNDEFINED; ///< Format of the imageView.
        uint32_t    width  = 0;
        uint32_t    height = 0;
        bool        owned; ///< If true this object owns imageView and must destroy it.
        // Owning texture, set by Texture::createView(). Lets beginRenderPass()/end()
        // write back the layout an offscreen pass leaves the image in, so later calls
        // (e.g. Texture::download()) transition from the real layout instead of a stale
        // VK_IMAGE_LAYOUT_UNDEFINED — see RenderPassEncoder::end()'s offscreen branch.
        // Null for views created via TextureView::fromNative(), which aren't backed by
        // a TextureHandle; those aren't tracked and end() simply skips the write-back.
        TextureHandle* ownerTexture = nullptr;
    };

}
