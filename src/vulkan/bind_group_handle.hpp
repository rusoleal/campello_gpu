#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct BindGroupHandle {
        VkDescriptorSet  descriptorSet;
        // Set when createBindGroup(persistent=true) so ~BindGroup() can call
        // vkFreeDescriptorSets. Left null for per-frame-pool bind groups
        // (reclaimed by vkResetDescriptorPool in beginFrameRing()).
        VkDevice         ownerDevice = VK_NULL_HANDLE;
        VkDescriptorPool ownerPool   = VK_NULL_HANDLE;
    };

}
