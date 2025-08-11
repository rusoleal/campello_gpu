#pragma once

#include <stdint.h>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/aspect.hpp>
#include <campello_gpu/texture_view.hpp>

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

        std::shared_ptr<TextureView> createView(PixelFormat format, uint32_t arrayLayerCount=-1, Aspect aspect=Aspect::all, uint32_t baseArrayLayer=0, uint32_t baseMipLevel=0, TextureType dimension=TextureType::tt2d);
        bool upload(uint64_t offset, uint64_t length, void *data);

        uint32_t getDepthOrarrayLayers();
        TextureType getDimension();
        PixelFormat getFormat();
        uint32_t getWidth();
        uint32_t getHeight();
        uint32_t getMipLevelCount();
        uint32_t getSampleCount();
        TextureUsage getUsage();


    };
}
