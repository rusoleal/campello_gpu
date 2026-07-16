#include "common.hpp"
#include <campello_gpu/fence.hpp>

using namespace systems::leal::campello_gpu;

Fence::Fence(void *pd) : native(pd) {}

Fence::~Fence() {
    if (native != nullptr) {
        auto *data = static_cast<VulkanFenceData *>(native);
        if (data->fence != VK_NULL_HANDLE && data->device != VK_NULL_HANDLE) {
            vkDestroyFence(data->device, data->fence, nullptr);
        }
        delete data;
    }
}

bool Fence::wait(uint64_t timeoutNs) {
    if (native == nullptr) return true;
    auto *data = static_cast<VulkanFenceData *>(native);

    VkResult result = vkWaitForFences(data->device, 1, &data->fence, VK_TRUE, timeoutNs);
    // A lost device will never make further progress on this fence, so further
    // waiting can't help -- treat it the same as a normal signal (wait()
    // returning true does not imply success; see didFail()'s doc comment).
    return (result == VK_SUCCESS || result == VK_ERROR_DEVICE_LOST);
}

bool Fence::isSignaled() const {
    if (native == nullptr) return true;
    auto *data = static_cast<VulkanFenceData *>(native);
    VkResult result = vkGetFenceStatus(data->device, data->fence);
    return (result == VK_SUCCESS || result == VK_ERROR_DEVICE_LOST);
}

bool Fence::didFail() const {
    if (native == nullptr) return false;
    auto *data = static_cast<VulkanFenceData *>(native);
    // Vulkan has no per-submission failure status the way Metal's
    // MTLCommandBuffer::status() does -- VK_ERROR_DEVICE_LOST means the whole
    // VkDevice (not just this fence's submission) is gone and must be
    // recreated. It's the closest and only equivalent signal available.
    return vkGetFenceStatus(data->device, data->fence) == VK_ERROR_DEVICE_LOST;
}

std::string Fence::failureReason() const {
    if (!didFail()) return {};
    return "VK_ERROR_DEVICE_LOST: the Vulkan device was lost (GPU hang/TDR, "
           "driver crash, or hardware fault); this VkDevice and all resources "
           "created from it are unusable and must be destroyed and recreated.";
}
