#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    VkInstance getInstance();

    struct DeviceData {
        VkDevice device;
        VkQueue graphicsQueue;
        VkPhysicalDevice physicalDevice;
    };

}