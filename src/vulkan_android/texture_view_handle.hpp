#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct TextureViewHandle {
        VkDevice    device;
        VkImageView imageView;
        bool        owned; ///< If true this object owns imageView and must destroy it.
    };

}
