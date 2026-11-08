#include <cstring>
#include <campello_gpu/buffer.hpp>
#include "buffer_handle.hpp"
#include "common.hpp"

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

// Copies [offset, offset+length) of `source` into a new MapRead buffer and
// submits the copy. Returns the readback buffer (nullptr on failure).
static WGPUBuffer submitReadbackCopy(BufferHandle* handle, uint64_t offset, uint64_t length) {
    WGPUBufferDescriptor bufDesc{};
    bufDesc.size = length;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(handle->deviceData->device, &bufDesc);
    if (!readback) return nullptr;

    WGPUCommandEncoderDescriptor encDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(handle->deviceData->device, &encDesc);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, handle->buffer, offset, readback, 0, length);

    WGPUCommandBufferDescriptor cmdDesc{};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(handle->deviceData->queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    return readback;
}

static bool copyOutMapped(WGPUBuffer readback, uint64_t length, void* data) {
    const void* mapped = wgpuBufferGetConstMappedRange(readback, 0, static_cast<size_t>(length));
    if (mapped) memcpy(data, mapped, static_cast<size_t>(length));
    wgpuBufferUnmap(readback);
    return mapped != nullptr;
}

bool Buffer::download(uint64_t offset, uint64_t length, void* data) {
    auto* handle = static_cast<BufferHandle*>(native);
    if (!handle->deviceData || !handle->deviceData->device || !handle->deviceData->queue) return false;
    if (offset + length > handle->size) return false;

    WGPUBuffer readback = submitReadbackCopy(handle, offset, length);
    if (!readback) return false;

    // mapAsync resolves once the GPU has finished the submitted copy.
    WGPUAsyncResult result;
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, static_cast<size_t>(length),
                       makeCallbackInfo<WGPUBufferMapCallbackInfo>(&result));
    waitForAsync(result);

    bool ok = result.status == static_cast<int>(WGPUMapAsyncStatus_Success) &&
              copyOutMapped(readback, length, data);
    wgpuBufferRelease(readback);
    return ok;
}

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

    WGPUBuffer readback = submitReadbackCopy(handle, offset, length);
    if (!readback) {
        callback(false);
        return;
    }

    auto* result = newAsyncResult([readback, length, data, callback = std::move(callback)](WGPUAsyncResult& r) {
        bool ok = r.status == static_cast<int>(WGPUMapAsyncStatus_Success) &&
                  copyOutMapped(readback, length, data);
        wgpuBufferRelease(readback);
        callback(ok);
    });
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, static_cast<size_t>(length),
                       makeCallbackInfo<WGPUBufferMapCallbackInfo>(result));
}
