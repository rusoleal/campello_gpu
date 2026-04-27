#include <campello_gpu/fence.hpp>
#include "fence_handle.hpp"

using namespace systems::leal::campello_gpu;

Fence::Fence(void* pd) {
    native = pd;
}

Fence::~Fence() {
    auto* handle = static_cast<FenceHandle*>(native);
    delete handle;
}

bool Fence::wait(uint64_t timeoutNs) {
    (void)timeoutNs;
    auto* handle = static_cast<FenceHandle*>(native);
    // WebGPU does not expose CPU-writable fences in standard API.
    // The fence is signaled immediately on submit.
    return handle->signaled.load();
}

bool Fence::isSignaled() const {
    auto* handle = static_cast<FenceHandle*>(native);
    return handle->signaled.load();
}
