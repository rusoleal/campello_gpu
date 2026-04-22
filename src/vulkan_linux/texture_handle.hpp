#pragma once

#include <memory>
#include <vulkan/vulkan.h>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/texture_type.hpp>

namespace systems::leal::campello_gpu {

    struct DeviceData;  // Forward declaration

    struct TextureHandle {
        VkDevice               device;
        VkPhysicalDevice       physicalDevice;
        VkCommandPool          commandPool;
        VkQueue                graphicsQueue;
        std::shared_ptr<Buffer> buffer;
        VkImage                image;
        VkDeviceMemory         imageMemory;
        VkImageView            defaultView; ///< Default view created at creation time (destroyed with texture).
        VkImageLayout          currentLayout; ///< Tracked layout for barrier transitions.
        uint32_t               width;
        uint32_t               height;
        uint32_t               depth;
        uint32_t               arrayLayers;
        uint32_t               mipLevels;
        uint32_t               samples;
        PixelFormat            pixelFormat;
        TextureUsage           usage;
        TextureType            textureType;
        uint64_t               allocatedSize;  // Actual GPU memory allocated
        DeviceData*            deviceData;     // For metrics tracking on destruction
    };

}
