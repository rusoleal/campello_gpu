#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct AccelerationStructureHandle {
        VkDevice                   device                = VK_NULL_HANDLE;
        VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
        VkBuffer                   buffer                = VK_NULL_HANDLE;
        VkDeviceMemory             memory                = VK_NULL_HANDLE;
        uint64_t                   buildScratchSize      = 0;
        uint64_t                   updateScratchSize     = 0;
        VkDeviceAddress            deviceAddress         = 0;
    };

}
