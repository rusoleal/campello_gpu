#pragma once

#include <memory>
#include <vulkan/vulkan.h>
#include <campello_gpu/buffer.hpp>

namespace systems::leal::campello_gpu {

    struct TextureHandle {
        VkDevice device;
        std::shared_ptr<Buffer> buffer;
        VkImage image;
    };

}