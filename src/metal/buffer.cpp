#include "Metal.hpp"
#include <campello_gpu/buffer.hpp>
#include "buffer_handle.hpp"

using namespace systems::leal::campello_gpu;

Buffer::Buffer(void *pd) {
    native = pd;
}

Buffer::~Buffer() {
    if (native != nullptr) {
        auto handle = (MetalBufferHandle *)native;
        
        // Phase 2: Update memory tracking
        if (handle->deviceData) {
            handle->deviceData->bufferCount--;
            handle->deviceData->bufferBytes.fetch_sub(handle->size);
        }
        
        handle->buffer->release();
        delete handle;
    }
}

uint64_t Buffer::getLength() {
    auto handle = (MetalBufferHandle *)native;
    return handle->buffer->length();
}

bool Buffer::upload(uint64_t offset, uint64_t size, void *data) {
    auto handle = (MetalBufferHandle *)native;
    uint8_t *dst = (uint8_t *)(handle->buffer->contents());
    memcpy(dst+offset,data, size);

    handle->buffer->didModifyRange(NS::Range::Make(offset, size));
    return true;
}

bool Buffer::download(uint64_t offset, uint64_t length, void *data) {
    if (!native || !data) return false;
    auto handle = (MetalBufferHandle *)native;
    auto *buf = handle->buffer;

    // Check bounds
    if (offset + length > buf->length()) return false;

    // MTLResourceStorageModeManaged buffers keep a separate CPU-visible copy
    // that Metal does not update automatically after a GPU write; an explicit
    // blit-encoder synchronizeResource: + wait is required before contents()
    // reflects the GPU side. Shared-mode buffers (mapRead/mapWrite, or any
    // buffer on iOS) are always coherent, so this is skipped there.
    if (buf->storageMode() == MTL::StorageModeManaged && handle->deviceData) {
        auto *commandQueue = handle->deviceData->commandQueue;
        auto *cmdBuffer = commandQueue->commandBuffer();
        if (!cmdBuffer) return false;

        auto *blitEncoder = cmdBuffer->blitCommandEncoder();
        if (!blitEncoder) return false;

        blitEncoder->synchronizeResource(buf);
        blitEncoder->endEncoding();

        cmdBuffer->commit();
        cmdBuffer->waitUntilCompleted();
    }

    // Get buffer contents
    uint8_t *src = static_cast<uint8_t *>(buf->contents());
    if (!src) return false;

    // Copy data
    memcpy(data, src + offset, length);
    return true;
}

void Buffer::downloadAsync(uint64_t offset, uint64_t length, void *data,
                           std::function<void(bool)> callback) {
    callback(download(offset, length, data));
}

