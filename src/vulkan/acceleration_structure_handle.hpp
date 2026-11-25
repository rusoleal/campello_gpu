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
        VkAccelerationStructureTypeKHR type              = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        // Built with allowCompaction: each build writes the compacted size
        // into this query (see CommandEncoder::buildAccelerationStructure()).
        VkQueryPool                compactedSizeQuery    = VK_NULL_HANDLE;
        bool                       compactedSizeRecorded = false;
        // Created by Device::createCompactedAccelerationStructure(): copies
        // into it compact.
        bool                       compacted             = false;
    };

}
