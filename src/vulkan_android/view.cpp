#include <android/log.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include "common.hpp"
#include <campello_gpu/view.hpp>

using namespace systems::leal::campello_gpu;

View::View(void *pd) {
    this->native = pd;
}

std::shared_ptr<View> View::createView(struct ANativeWindow *window, std::shared_ptr<Device> device) {

    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","View::createView");

    const VkAndroidSurfaceCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .window = window
    };

    VkSurfaceKHR surface;
    if (vkCreateAndroidSurfaceKHR(getInstance(), &create_info, nullptr, &surface) != VK_SUCCESS) {
        return nullptr;
    }

    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","vkCreateAndroidSurfaceKHR() success.");

    /*VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        ((DeviceData *)device->getNative())->physicalDevice,
        surface,
        &surfaceCapabilities
    ) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","vkGetPhysicalDeviceSurfaceCapabilitiesKHR error");
        return nullptr;
    }

    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","surface capabilities:");
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","  minImageCount: %d",surfaceCapabilities.minImageCount);
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","  maxImageCount: %d",surfaceCapabilities.maxImageCount);
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","  currentExtent: [%d, %d]",surfaceCapabilities.currentExtent.width, surfaceCapabilities.currentExtent.height);
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","  minImageExtent: [%d, %d]",surfaceCapabilities.minImageExtent.width, surfaceCapabilities.minImageExtent.height);
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","  maxImageExtent: [%d, %d]",surfaceCapabilities.maxImageExtent.width, surfaceCapabilities.maxImageExtent.height);
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","  maxImageArrayLayers: %d",surfaceCapabilities.maxImageArrayLayers);
*/
    return std::shared_ptr<View>(new View((void *)surface));

}
