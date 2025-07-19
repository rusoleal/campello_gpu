#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {
        struct ShaderModuleHandle {
        VkDevice device;
        VkShaderModule shaderModule;
    };
}

