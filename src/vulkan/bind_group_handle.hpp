#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct BindGroupHandle {
        VkDescriptorSet descriptorSet;
        // Lifetime is managed by the DeviceData descriptor pool.
    };

}
