#include <android/log.h>
#include <memory>
#include <campello_gpu/buffer.hpp>
#include "buffer_handle.hpp"

using namespace systems::leal::campello_gpu;

Buffer::Buffer(void *pd) {
    this->native = pd;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","Buffer::Buffer()");
}

Buffer::~Buffer() {

    auto data = (BufferHandle *)this->native;

    vkFreeMemory(data->device, data->memory, nullptr);

    vkDestroyBuffer(data->device, data->buffer, nullptr);

    delete data;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","Buffer::~Buffer()");
}

uint64_t Buffer::getLength() {
    return 0;
}

bool Buffer::upload(uint64_t offset, uint64_t length, void *data) {
    
    auto handle = (BufferHandle *)this->native;

    void *p;
    if (vkMapMemory(handle->device, handle->memory, offset, length, 0, &p) != VK_SUCCESS) {
        return false;
    }

    memcpy(p,data,length);

    // flush memory
    VkMappedMemoryRange range;
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.pNext = nullptr;
    range.memory = handle->memory;
    range.offset = offset;
    range.size = length;
    if (vkFlushMappedMemoryRanges(handle->device, 1, &range) != VK_SUCCESS) {
        return false;
    }

    vkUnmapMemory(handle->device, handle->memory);

    return true;
}

