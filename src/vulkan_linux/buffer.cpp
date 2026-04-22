#include <memory>
#include <campello_gpu/buffer.hpp>
#include "buffer_handle.hpp"
#include "common.hpp"

using namespace systems::leal::campello_gpu;

Buffer::Buffer(void *pd) {
    this->native = pd;
    
}

Buffer::~Buffer() {
    auto data = (BufferHandle *)this->native;
    
    // Phase 2: Update memory tracking
    if (data->deviceData) {
        data->deviceData->bufferCount--;
        data->deviceData->bufferBytes.fetch_sub(data->allocatedSize);
    }
    
    vkFreeMemory(data->device, data->memory, nullptr);
    vkDestroyBuffer(data->device, data->buffer, nullptr);
    delete data;
    
}

uint64_t Buffer::getLength() {
    auto data = (BufferHandle *)this->native;
    return data->size;
}

bool Buffer::upload(uint64_t offset, uint64_t length, void *data) {
    auto handle = (BufferHandle *)this->native;

    void *p;
    if (vkMapMemory(handle->device, handle->memory, offset, length, 0, &p) != VK_SUCCESS) {
        return false;
    }

    memcpy(p, data, length);

    VkMappedMemoryRange range;
    range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.pNext  = nullptr;
    range.memory = handle->memory;
    range.offset = offset;
    range.size   = length;
    if (vkFlushMappedMemoryRanges(handle->device, 1, &range) != VK_SUCCESS) {
        return false;
    }

    vkUnmapMemory(handle->device, handle->memory);
    return true;
}

bool Buffer::download(uint64_t offset, uint64_t length, void *data) {
    auto handle = (BufferHandle *)this->native;

    void *p;
    if (vkMapMemory(handle->device, handle->memory, offset, length, 0, &p) != VK_SUCCESS) {
        return false;
    }

    // Invalidate so any GPU writes are visible to the CPU.
    // This is a no-op for HOST_COHERENT memory but is correct practice.
    VkMappedMemoryRange range;
    range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.pNext  = nullptr;
    range.memory = handle->memory;
    range.offset = offset;
    range.size   = length;
    vkInvalidateMappedMemoryRanges(handle->device, 1, &range);

    memcpy(data, p, length);
    vkUnmapMemory(handle->device, handle->memory);
    return true;
}
