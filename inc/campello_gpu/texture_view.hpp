#pragma once

#include <memory>

namespace systems::leal::campello_gpu
{
    class Device;

    /**
     * @brief A typed, sub-resource view into a `Texture`.
     *
     * A `TextureView` describes which part of a `Texture` (format, aspect, mip range,
     * array-layer range) is visible to the pipeline. It is required wherever a texture
     * must be bound — render-pass color/depth attachments, sampled textures, storage
     * images, etc.
     *
     * **Creating a view from a library texture:**
     * @code
     * auto view = texture->createView(PixelFormat::rgba8unorm);
     * @endcode
     *
     * **Wrapping a platform-native texture (interop):**
     * @code
     * // Metal example — bridge a CAMetalDrawable texture into a TextureView
     * auto view = TextureView::fromNative((__bridge void *)drawable.texture);
     * @endcode
     */
    class TextureView
    {
    private:
        friend class Device;
        friend class Texture;
        friend class CommandEncoder;
        void *native;

        TextureView(void *pd);

    public:
        ~TextureView();

        /**
         * @brief Wraps a platform-native texture handle in a `TextureView`.
         *
         * Use this to bridge a texture obtained from a platform API (e.g. a Metal
         * drawable's `id<MTLTexture>`, a Vulkan `VkImageView`) into the campello_gpu
         * type system. The native object is **retained** on construction and
         * **released** when the `TextureView` is destroyed.
         *
         * The caller must cast the native handle to `void*` before passing it.
         * On Metal with ARC, use `(__bridge void*)` to avoid ownership transfer:
         * @code
         * auto view = TextureView::fromNative((__bridge void *)drawable.texture);
         * @endcode
         *
         * @param nativeTex Platform-specific texture handle cast to `void*`.
         * @return A `TextureView` wrapping the native texture.
         */
        static std::shared_ptr<TextureView> fromNative(void *nativeTex);
    };
}
