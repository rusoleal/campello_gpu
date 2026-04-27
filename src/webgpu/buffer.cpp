#include <cstring>
#include <campello_gpu/buffer.hpp>
#include "buffer_handle.hpp"
#include "common.hpp"
#include <emscripten.h>

using namespace systems::leal::campello_gpu;

Buffer::Buffer(void* pd) {
    native = pd;
}

Buffer::~Buffer() {
    auto* handle = static_cast<BufferHandle*>(native);
    if (handle->deviceData) {
        handle->deviceData->bufferCount--;
        handle->deviceData->bufferBytes.fetch_sub(handle->allocatedSize);
    }
    if (handle->buffer) {
        wgpuBufferRelease(handle->buffer);
    }
    delete handle;
}

uint64_t Buffer::getLength() {
    auto* handle = static_cast<BufferHandle*>(native);
    return handle->size;
}

bool Buffer::upload(uint64_t offset, uint64_t length, void* data) {
    auto* handle = static_cast<BufferHandle*>(native);
    if (!handle->deviceData || !handle->deviceData->queue) return false;
    wgpuQueueWriteBuffer(handle->deviceData->queue, handle->buffer, offset, data, length);
    return true;
}

// Helper for async buffer mapping with ASYNCIFY
struct MapContext {
    bool done = false;
    WGPUBufferMapAsyncStatus status = WGPUBufferMapAsyncStatus_Unknown;
};

static void onBufferMapped(WGPUBufferMapAsyncStatus status, void* userdata) {
    auto* ctx = static_cast<MapContext*>(userdata);
    ctx->status = status;
    ctx->done = true;
}

bool Buffer::download(uint64_t offset, uint64_t length, void* data) {
    auto* handle = static_cast<BufferHandle*>(native);
    if (!handle->deviceData || !handle->deviceData->device || !handle->deviceData->queue) return false;
    if (offset + length > handle->size) return false;

    // 1. Create readback buffer
    WGPUBufferDescriptor bufDesc{};
    bufDesc.size = length;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(handle->deviceData->device, &bufDesc);
    if (!readback) return false;

    // 2. Record copy source -> readback
    WGPUCommandEncoderDescriptor encDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(handle->deviceData->device, &encDesc);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, handle->buffer, offset, readback, 0, length);

    WGPUCommandBufferDescriptor cmdDesc{};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuCommandEncoderRelease(encoder);

    // 3. Submit and wait for GPU completion
    wgpuQueueSubmit(handle->deviceData->queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);

    MapContext idleCtx;
    wgpuQueueOnSubmittedWorkDone(handle->deviceData->queue,
        [](WGPUQueueWorkDoneStatus status, void* userdata) {
            (void)status;
            auto* ctx = static_cast<MapContext*>(userdata);
            ctx->done = true;
        }, &idleCtx);

    while (!idleCtx.done) {
        emscripten_sleep(1);
    }

    // 4. Map buffer and read data
    MapContext mapCtx;
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, length, onBufferMapped, &mapCtx);

    while (!mapCtx.done) {
        emscripten_sleep(1);
    }

    if (mapCtx.status != WGPUBufferMapAsyncStatus_Success) {
        wgpuBufferRelease(readback);
        return false;
    }

    const void* mapped = wgpuBufferGetConstMappedRange(readback, 0, length);
    if (mapped) {
        memcpy(data, mapped, length);
    }

    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);

    return mapped != nullptr;
}

// Async download — chains wgpuQueueOnSubmittedWorkDone -> wgpuBufferMapAsync -> user callback
struct BufferDownloadAsyncData {
    WGPUBuffer readback;
    void* data;
    uint64_t length;
    std::function<void(bool)> callback;
};

void Buffer::downloadAsync(uint64_t offset, uint64_t length, void* data,
                           std::function<void(bool)> callback) {
    auto* handle = static_cast<BufferHandle*>(native);
    if (!handle->deviceData || !handle->deviceData->device || !handle->deviceData->queue) {
        callback(false);
        return;
    }
    if (offset + length > handle->size) {
        callback(false);
        return;
    }

    // 1. Create readback buffer
    WGPUBufferDescriptor bufDesc{};
    bufDesc.size = length;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(handle->deviceData->device, &bufDesc);
    if (!readback) {
        callback(false);
        return;
    }

    // 2. Record copy source -> readback
    WGPUCommandEncoderDescriptor encDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(handle->deviceData->device, &encDesc);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, handle->buffer, offset, readback, 0, length);

    WGPUCommandBufferDescriptor cmdDesc{};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuCommandEncoderRelease(encoder);

    // 3. Submit
    wgpuQueueSubmit(handle->deviceData->queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);

    // 4. Chain async: queue done -> map buffer -> copy -> callback
    auto* asyncData = new BufferDownloadAsyncData{readback, data, length, std::move(callback)};

    wgpuQueueOnSubmittedWorkDone(handle->deviceData->queue,
        [](WGPUQueueWorkDoneStatus, void* userdata) {
            auto* d = static_cast<BufferDownloadAsyncData*>(userdata);
            wgpuBufferMapAsync(d->readback, WGPUMapMode_Read, 0, d->length,
                [](WGPUBufferMapAsyncStatus status, void* userdata2) {
                    auto* d2 = static_cast<BufferDownloadAsyncData*>(userdata2);
                    if (status == WGPUBufferMapAsyncStatus_Success) {
                        const void* mapped = wgpuBufferGetConstMappedRange(d2->readback, 0, d2->length);
                        if (mapped) {
                            memcpy(d2->data, mapped, d2->length);
                        }
                        wgpuBufferUnmap(d2->readback);
                        d2->callback(mapped != nullptr);
                    } else {
                        d2->callback(false);
                    }
                    wgpuBufferRelease(d2->readback);
                    delete d2;
                }, d);
        }, asyncData);
}
