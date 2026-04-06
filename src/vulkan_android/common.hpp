#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include <android/native_window.h>

namespace systems::leal::campello_gpu {

    VkInstance getInstance();

    struct DeviceData {
        VkDevice                  device;
        VkQueue                   graphicsQueue;
        VkPhysicalDevice          physicalDevice;
        VkSurfaceKHR              surface;
        VkSwapchainKHR            swapchain;
        VkExtent2D                imageExtent;
        std::vector<VkImage>      swapchainImages;
        std::vector<VkImageView>  swapchainImageViews;
        VkSurfaceFormatKHR        surfaceFormat;
        VkCommandPool             commandPool;
        VkDescriptorPool          descriptorPool;
        VkSemaphore               imageAvailableSemaphore;
        VkSemaphore               renderFinishedSemaphore;
        ANativeWindow            *window               = nullptr;
        uint32_t                  queueFamilyIndex     = 0;
    };

}
