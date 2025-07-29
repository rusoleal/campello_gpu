#pragma once

#include <stdint.h>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_usage.hpp>

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

        bool upload(uint64_t offset, uint64_t length, void *data);

        //PixelFormat getPixelFormat();
        //uint64_t getWidth();
        //uint64_t getHeight();
        //TextureUsage getUsageMode();
    };
}
