#pragma once

#include <vector>
#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu {

    VkInstance getInstance();

    struct DeviceData {
        VkDevice device;
        VkQueue graphicsQueue;
        VkPhysicalDevice physicalDevice;
        VkSurfaceKHR surface;
        VkSwapchainKHR swapchain;
        VkExtent2D imageExtent;
        std::vector<VkImageView> swapchainImageViews;
        VkSurfaceFormatKHR surfaceFormat;
        VkCommandPool commandPool;
    };

}