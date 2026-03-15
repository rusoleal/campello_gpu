#pragma once

#include <memory>

namespace systems::leal::campello_gpu
{
    class Device;

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

        // Bridge: wrap a platform-native texture handle (e.g. id<MTLTexture> on Metal,
        // VkImageView on Vulkan) into a TextureView. The native object is retained.
        static std::shared_ptr<TextureView> fromNative(void *nativeTex);
    };
}
