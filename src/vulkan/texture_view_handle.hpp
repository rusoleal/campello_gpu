#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    struct TextureViewHandle {
        VkDevice    device;
        VkImageView imageView;
        VkImage     image  = VK_NULL_HANDLE; ///< Backing image (for layout transitions in beginRenderPass).
        VkFormat    format = VK_FORMAT_UNDEFINED; ///< Format of the imageView.
        uint32_t    width  = 0;
        uint32_t    height = 0;
        bool        owned; ///< If true this object owns imageView and must destroy it.
    };

}
