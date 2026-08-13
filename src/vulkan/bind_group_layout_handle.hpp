#pragma once

#include <unordered_map>
#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct BindGroupLayoutHandle {
        VkDevice              device;
        VkDescriptorSetLayout layout;
        // binding -> the descriptor type this layout actually declared for it.
        // Populated by Device::createBindGroupLayout(); consulted by
        // Device::createBindGroup() to skip entries whose resource type doesn't
        // match — see that call site's comment for why a mismatch can legitimately
        // occur (Metal's separate texture/buffer/sampler argument-index spaces let
        // campello_renderer reuse one binding number for two different resource
        // kinds across pipeline variants, which is invalid on Vulkan's single
        // per-binding-type descriptor set model).
        std::unordered_map<uint32_t, VkDescriptorType> bindingTypes;
    };

}
