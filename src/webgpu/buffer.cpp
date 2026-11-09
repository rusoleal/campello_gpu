#include <algorithm>
#include <cstring>
#include <vector>
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

// WebGPU alignment rules: queue writes and buffer copies need 4-byte offsets
// and sizes; mapAsync needs an 8-byte offset and a 4-byte size. The helpers
// below widen a range to those rules (clamped to the buffer) and the callers
// copy out / in just the bytes that were asked for.
static uint64_t alignDown(uint64_t v, uint64_t a) { return v - v % a; }
static uint64_t alignUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

struct ByteRange {
    uint64_t offset = 0;
    uint64_t size = 0;
};

static ByteRange widenRange(uint64_t offset, uint64_t length, uint64_t offsetAlign,
                            uint64_t bufferSize) {
    ByteRange r;
    r.offset = alignDown(offset, offsetAlign);
    r.size = std::min(alignUp(offset + length, 4), bufferSize) - r.offset;
    return r;
}

static bool mapBlocking(WGPUBuffer buffer, WGPUMapMode mode, const ByteRange& range) {
    WGPUAsyncResult result;
    wgpuBufferMapAsync(buffer, mode, static_cast<size_t>(range.offset), static_cast<size_t>(range.size),
                       makeCallbackInfo<WGPUBufferMapCallbackInfo>(&result));
    waitForAsync(result);
    return result.status == static_cast<int>(WGPUMapAsyncStatus_Success);
}

bool Buffer::upload(uint64_t offset, uint64_t length, void* data) {
    auto* handle = static_cast<BufferHandle*>(native);
    if (!handle->deviceData || !handle->deviceData->queue) return false;
    if (offset + length > handle->size) return false;
    if (length == 0) return true;

    // MapWrite buffers can't take CopyDst, so they're written through a map.
    if (handle->usage & WGPUBufferUsage_MapWrite) {
        ByteRange range = widenRange(offset, length, 8, handle->allocatedSize);
        if (!mapBlocking(handle->buffer, WGPUMapMode_Write, range)) return false;
        auto* mapped = static_cast<uint8_t*>(
            wgpuBufferGetMappedRange(handle->buffer, static_cast<size_t>(range.offset),
                                     static_cast<size_t>(range.size)));
        if (mapped) memcpy(mapped + (offset - range.offset), data, static_cast<size_t>(length));
        wgpuBufferUnmap(handle->buffer);
        return mapped != nullptr;
    }

    if (offset % 4 == 0 && length % 4 == 0) {
        wgpuQueueWriteBuffer(handle->deviceData->queue, handle->buffer, offset, data,
                             static_cast<size_t>(length));
        return true;
    }

    // Unaligned write: read the surrounding 4-byte-aligned range, patch it,
    // and write it back so neighbouring bytes are preserved.
    uint64_t writeOffset = alignDown(offset, 4);
    uint64_t writeEnd = alignUp(offset + length, 4);
    if (writeEnd > handle->allocatedSize) return false;  // allocations are whole words (createBuffer)
    std::vector<uint8_t> staging(static_cast<size_t>(writeEnd - writeOffset), 0);
    // Read back the bytes being preserved, up to the buffer's logical end
    // (the padding of the last word, past getLength(), has no content).
    const uint64_t readEnd = std::min(writeEnd, handle->size);
    if (readEnd > writeOffset && !download(writeOffset, readEnd - writeOffset, staging.data())) return false;
    memcpy(staging.data() + (offset - writeOffset), data, static_cast<size_t>(length));
    wgpuQueueWriteBuffer(handle->deviceData->queue, handle->buffer, writeOffset, staging.data(),
                         staging.size());
    return true;
}

// Copies `range` of the buffer into a new MapRead buffer and submits the copy.
// Returns the readback buffer (nullptr on failure).
static WGPUBuffer submitReadbackCopy(BufferHandle* handle, const ByteRange& range) {
    WGPUBufferDescriptor bufDesc{};
    bufDesc.size = range.size;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(handle->deviceData->device, &bufDesc);
    if (!readback) return nullptr;

    WGPUCommandEncoderDescriptor encDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(handle->deviceData->device, &encDesc);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, handle->buffer, range.offset, readback, 0, range.size);

    WGPUCommandBufferDescriptor cmdDesc{};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(handle->deviceData->queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    return readback;
}

// How a download reads its bytes: a MapRead buffer is mapped directly (it
// can't be a copy source); anything else is copied into a readback buffer.
struct DownloadPlan {
    WGPUBuffer mapped = nullptr;   // the buffer that gets mapped
    bool ownsMapped = false;       // true for a temporary readback buffer
    ByteRange mapRange;            // range passed to mapAsync
    uint64_t skip = 0;             // bytes to skip at the start of the mapped range
    uint64_t length = 0;
};

static bool planDownload(BufferHandle* handle, uint64_t offset, uint64_t length, DownloadPlan& plan) {
    plan.length = length;
    if (handle->usage & WGPUBufferUsage_MapRead) {
        plan.mapped = handle->buffer;
        plan.mapRange = widenRange(offset, length, 8, handle->allocatedSize);
        plan.skip = offset - plan.mapRange.offset;
        return true;
    }
    ByteRange copyRange = widenRange(offset, length, 4, handle->allocatedSize);
    plan.mapped = submitReadbackCopy(handle, copyRange);
    plan.ownsMapped = true;
    plan.mapRange = { 0, copyRange.size };
    plan.skip = offset - copyRange.offset;
    return plan.mapped != nullptr;
}

static bool finishDownload(const DownloadPlan& plan, bool mapOk, void* data) {
    bool ok = false;
    if (mapOk) {
        const auto* mapped = static_cast<const uint8_t*>(
            wgpuBufferGetConstMappedRange(plan.mapped, static_cast<size_t>(plan.mapRange.offset),
                                          static_cast<size_t>(plan.mapRange.size)));
        if (mapped) {
            memcpy(data, mapped + plan.skip, static_cast<size_t>(plan.length));
            ok = true;
        }
        wgpuBufferUnmap(plan.mapped);
    }
    if (plan.ownsMapped) wgpuBufferRelease(plan.mapped);
    return ok;
}

bool Buffer::download(uint64_t offset, uint64_t length, void* data) {
    auto* handle = static_cast<BufferHandle*>(native);
    if (!handle->deviceData || !handle->deviceData->device || !handle->deviceData->queue) return false;
    if (offset + length > handle->size) return false;
    if (length == 0) return true;

    DownloadPlan plan;
    if (!planDownload(handle, offset, length, plan)) return false;

    // mapAsync resolves once the GPU has finished any work using the buffer.
    bool mapOk = mapBlocking(plan.mapped, WGPUMapMode_Read, plan.mapRange);
    return finishDownload(plan, mapOk, data);
}

void Buffer::downloadAsync(uint64_t offset, uint64_t length, void* data,
                           std::function<void(bool)> callback) {
    auto* handle = static_cast<BufferHandle*>(native);
    if (!handle->deviceData || !handle->deviceData->device || !handle->deviceData->queue ||
        offset + length > handle->size) {
        callback(false);
        return;
    }
    if (length == 0) {
        callback(true);
        return;
    }

    DownloadPlan plan;
    if (!planDownload(handle, offset, length, plan)) {
        callback(false);
        return;
    }

    auto* result = newAsyncResult([plan, data, callback = std::move(callback)](WGPUAsyncResult& r) {
        bool mapOk = r.status == static_cast<int>(WGPUMapAsyncStatus_Success);
        callback(finishDownload(plan, mapOk, data));
    });
    wgpuBufferMapAsync(plan.mapped, WGPUMapMode_Read, static_cast<size_t>(plan.mapRange.offset),
                       static_cast<size_t>(plan.mapRange.size),
                       makeCallbackInfo<WGPUBufferMapCallbackInfo>(result));
}
