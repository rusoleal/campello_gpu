#pragma once

#include <campello_gpu/pixel_format.hpp>
#include <campello_gpu/texture_usage.hpp>

namespace systems::leal::campello_gpu
{

    class Device;

    class Texture
    {
    private:
        friend class Device;
        void *native;

        Texture(void *pd);

    public:
        ~Texture();

        PixelFormat getPixelFormat();
        uint64_t getWidth();
        uint64_t getHeight();
        TextureUsage getUsageMode();
    };
}
