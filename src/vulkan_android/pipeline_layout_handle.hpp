#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct PipelineLayoutHandle {
        VkDevice device;
        VkPipelineLayout layout;
    };

}