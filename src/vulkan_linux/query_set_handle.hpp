#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu
{
    struct QuerySetHandle
    {
        VkDevice device;
        VkQueryPool queryPool;
    };
}
