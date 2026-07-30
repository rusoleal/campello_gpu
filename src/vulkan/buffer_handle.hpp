#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct DeviceData;  // Forward declaration

    struct BufferHandle {
        VkDevice       device;
        VkBuffer       buffer;
        VkDeviceMemory memory;
        uint64_t       size;
        uint64_t       allocatedSize;  // Actual GPU memory allocated (may include alignment)
        DeviceData*    deviceData;     // For metrics tracking on destruction
        VkCommandPool  commandPool    = VK_NULL_HANDLE; ///< Pool for staging copy commands.
        VkQueue        graphicsQueue  = VK_NULL_HANDLE; ///< Queue for staging copy commands.
        bool           isDeviceLocal  = false; ///< True if memory is DEVICE_LOCAL.
        bool           isHostVisible  = false; ///< True if memory is HOST_VISIBLE (mappable).
    };

}
