#pragma once

#include <memory>
#include <vulkan/vulkan.h>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/texture_type.hpp>

namespace systems::leal::campello_gpu {

    struct TextureHandle {
        VkDevice               device;
        VkPhysicalDevice       physicalDevice;
        VkCommandPool          commandPool;
        VkQueue                graphicsQueue;
        std::shared_ptr<Buffer> buffer;
        VkImage                image;
        VkImageView            defaultView; ///< Default view created at creation time (destroyed with texture).
        VkImageLayout          currentLayout; ///< Tracked layout for barrier transitions.
        uint32_t               width;
        uint32_t               height;
        uint32_t               depth;
        uint32_t               mipLevels;
        uint32_t               samples;
        PixelFormat            pixelFormat;
        TextureUsage           usage;
        TextureType            textureType;
    };

}
