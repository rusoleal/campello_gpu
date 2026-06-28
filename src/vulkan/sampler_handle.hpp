#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu
{

    struct SamplerHandle
    {
        VkDevice device;
        VkSampler sampler;
    };
}
