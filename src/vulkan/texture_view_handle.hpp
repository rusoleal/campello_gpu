#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct TextureHandle;  // Forward declaration
    struct DeviceData;     // Forward declaration

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
        // Independent copy of ownerTexture->deviceData, taken at creation
        // time (see Texture::createView()) — NOT derived from ownerTexture
        // at use time. TextureView::~TextureView() needs DeviceData to
        // queue its deferred destroy (see PendingTextureDestroy in
        // common.hpp), but a TextureView's lifetime is independent of its
        // owning Texture's: nothing stops the owning Texture (and its
        // TextureHandle, deleted by Texture::~Texture()) from being
        // destroyed first, which would leave ownerTexture dangling. This
        // field sidesteps that entirely by never touching ownerTexture
        // outside of the (still-safe, view-is-actively-in-use) rendering
        // paths that already used it before this field existed.
        DeviceData* deviceData = nullptr;
        // The specific subresource this view exposes — set by Texture::createView().
        // beginRenderPass()/end() use these (not a hardcoded (0,0)) to transition the
        // *actual* targeted mip/layer when this view is used as a render-pass color
        // attachment; every other subresource of the owning image must otherwise stay
        // untouched by that transition, or a genuinely different subresource that also
        // happens to be attachment-bound elsewhere gets its layout corrupted instead.
        uint32_t baseArrayLayer = 0;
        uint32_t baseMipLevel   = 0;
    };

}
