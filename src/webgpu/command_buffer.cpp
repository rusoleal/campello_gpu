#include <campello_gpu/command_buffer.hpp>
#include "command_buffer_handle.hpp"

using namespace systems::leal::campello_gpu;

CommandBuffer::CommandBuffer(void* pd) {
    native = pd;
}

CommandBuffer::~CommandBuffer() {
    auto* handle = static_cast<CommandBufferHandle*>(native);
    if (handle->commandBuffer) {
        wgpuCommandBufferRelease(handle->commandBuffer);
    }
    // handle->timing may still be referenced by an in-flight readback started
    // in Device::submit(); it is released when that completes.
    delete handle;
}

// GPU time spans the first timed pass's begin to the last timed pass's end
// (see GpuTimingState in common.hpp). Timing needs the "timestamp-query"
// feature and is only available after Device::submit(); otherwise returns 0.
uint64_t CommandBuffer::getGPUExecutionTime() {
    auto* handle = static_cast<CommandBufferHandle*>(native);
    auto timing = handle->timing;
    if (!timing || !timing->submitted) return 0;

    // The readback was started by submit(); yield until it resolves.
    while (!timing->done) {
        emscripten_sleep(1);
    }
    return timing->totalNs;
}

void CommandBuffer::getGPUExecutionTimeAsync(std::function<void(uint64_t)> callback) {
    auto* handle = static_cast<CommandBufferHandle*>(native);
    auto timing = handle->timing;
    if (!timing || !timing->submitted) {
        callback(0);
        return;
    }
    if (timing->done) {
        callback(timing->totalNs);
        return;
    }
    timing->waiters.push_back(std::move(callback));
}
