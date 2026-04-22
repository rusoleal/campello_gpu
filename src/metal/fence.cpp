#include "common.hpp"
#include <campello_gpu/fence.hpp>

using namespace systems::leal::campello_gpu;

Fence::Fence(void *pd) : native(pd) {}

Fence::~Fence() {
    if (native != nullptr) {
        delete static_cast<MetalFenceData *>(native);
    }
}

bool Fence::wait(uint64_t timeoutNs) {
    if (native == nullptr) return true;
    auto *data = static_cast<MetalFenceData *>(native);

    if (data->signaled.load(std::memory_order_acquire)) return true;

    std::unique_lock<std::mutex> lock(data->mutex);
    if (timeoutNs == UINT64_MAX) {
        data->cv.wait(lock, [data]() {
            return data->signaled.load(std::memory_order_acquire);
        });
        return true;
    } else {
        return data->cv.wait_for(lock, std::chrono::nanoseconds(timeoutNs), [data]() {
            return data->signaled.load(std::memory_order_acquire);
        });
    }
}

bool Fence::isSignaled() const {
    if (native == nullptr) return true;
    auto *data = static_cast<MetalFenceData *>(native);
    return data->signaled.load(std::memory_order_acquire);
}
