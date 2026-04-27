#include <campello_gpu/command_buffer.hpp>
#include "command_buffer_handle.hpp"
#include <emscripten.h>
#include <cstring>

using namespace systems::leal::campello_gpu;

CommandBuffer::CommandBuffer(void* pd) {
    native = pd;
}

CommandBuffer::~CommandBuffer() {
    auto* handle = static_cast<CommandBufferHandle*>(native);
    if (handle->commandBuffer) {
        wgpuCommandBufferRelease(handle->commandBuffer);
    }
    if (handle->timestampReadbackBuffer) {
        wgpuBufferRelease(handle->timestampReadbackBuffer);
    }
    delete handle;
}

uint64_t CommandBuffer::getGPUExecutionTime() {
    auto* handle = static_cast<CommandBufferHandle*>(native);
    if (!handle->hasTimingData || !handle->timestampReadbackBuffer || !handle->deviceData) {
        return 0;
    }

    // Wait for GPU work to complete
    struct DoneCtx { bool done; };
    DoneCtx waitCtx{false};
    wgpuQueueOnSubmittedWorkDone(handle->deviceData->queue,
        [](WGPUQueueWorkDoneStatus, void* userdata) {
            static_cast<DoneCtx*>(userdata)->done = true;
        }, &waitCtx);
    while (!waitCtx.done) {
        emscripten_sleep(1);
    }

    // Map readback buffer
    struct MapCtx { bool done; WGPUBufferMapAsyncStatus status; };
    MapCtx mapCtx{false, WGPUBufferMapAsyncStatus_Unknown};
    wgpuBufferMapAsync(handle->timestampReadbackBuffer, WGPUMapMode_Read, 0,
                       2 * sizeof(uint64_t),
        [](WGPUBufferMapAsyncStatus status, void* userdata) {
            auto* ctx = static_cast<MapCtx*>(userdata);
            ctx->status = status;
            ctx->done = true;
        }, &mapCtx);
    while (!mapCtx.done) {
        emscripten_sleep(1);
    }

    if (mapCtx.status != WGPUBufferMapAsyncStatus_Success) {
        return 0;
    }

    const uint64_t* timestamps = static_cast<const uint64_t*>(
        wgpuBufferGetConstMappedRange(handle->timestampReadbackBuffer, 0, 2 * sizeof(uint64_t)));
    if (!timestamps) {
        return 0;
    }

    uint64_t startTick = timestamps[0];
    uint64_t endTick = timestamps[1];
    wgpuBufferUnmap(handle->timestampReadbackBuffer);

    if (endTick <= startTick) {
        return 0;
    }

    // WebGPU timestamp values are in nanoseconds
    return endTick - startTick;
}

struct TimingAsyncData {
    WGPUBuffer timestampReadbackBuffer;
    std::function<void(uint64_t)> callback;
};

void CommandBuffer::getGPUExecutionTimeAsync(std::function<void(uint64_t)> callback) {
    auto* handle = static_cast<CommandBufferHandle*>(native);
    if (!handle->hasTimingData || !handle->timestampReadbackBuffer || !handle->deviceData) {
        callback(0);
        return;
    }

    // Chain async: queue done -> map buffer -> read timestamps -> callback
    auto* asyncData = new TimingAsyncData{
        handle->timestampReadbackBuffer,
        std::move(callback)
    };

    wgpuQueueOnSubmittedWorkDone(handle->deviceData->queue,
        [](WGPUQueueWorkDoneStatus, void* userdata) {
            auto* d = static_cast<TimingAsyncData*>(userdata);
            wgpuBufferMapAsync(d->timestampReadbackBuffer, WGPUMapMode_Read, 0,
                               2 * sizeof(uint64_t),
                [](WGPUBufferMapAsyncStatus status, void* userdata2) {
                    auto* d2 = static_cast<TimingAsyncData*>(userdata2);
                    if (status != WGPUBufferMapAsyncStatus_Success) {
                        d2->callback(0);
                        delete d2;
                        return;
                    }
                    const uint64_t* timestamps = static_cast<const uint64_t*>(
                        wgpuBufferGetConstMappedRange(d2->timestampReadbackBuffer, 0, 2 * sizeof(uint64_t)));
                    if (!timestamps) {
                        d2->callback(0);
                        delete d2;
                        return;
                    }
                    uint64_t startTick = timestamps[0];
                    uint64_t endTick = timestamps[1];
                    wgpuBufferUnmap(d2->timestampReadbackBuffer);
                    if (endTick <= startTick) {
                        d2->callback(0);
                    } else {
                        d2->callback(endTick - startTick);
                    }
                    delete d2;
                }, d);
        }, asyncData);
}
