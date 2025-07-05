#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct BufferHandle {
        VkDevice device;
        VkBuffer buffer;
        VkDeviceMemory memory;
    };

}