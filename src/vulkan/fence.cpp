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
    return (result == VK_SUCCESS);
}

bool Fence::isSignaled() const {
    if (native == nullptr) return true;
    auto *data = static_cast<VulkanFenceData *>(native);
    return (vkGetFenceStatus(data->device, data->fence) == VK_SUCCESS);
}
