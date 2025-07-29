#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu
{
    struct ComputePipelineHandle
    {
        VkDevice device;
        VkPipeline pipeline;
    };
}
