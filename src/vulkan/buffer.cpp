#include <cstring>
#include <memory>
#include <mutex>
#include <campello_gpu/buffer.hpp>
#include "buffer_handle.hpp"
#include "common.hpp"

using namespace systems::leal::campello_gpu;

namespace {

// Allocate a small host-visible staging buffer for device-local copy operations.
// Takes the physical device's memory properties directly (see DeviceData::
// memoryProperties' doc comment) rather than a VkPhysicalDevice to re-query
// — they're fixed for the device's lifetime and this runs on every
// non-host-visible Buffer::upload()/download() call.
bool createStagingBuffer(VkDevice device,
                         const VkPhysicalDeviceMemoryProperties &memProperties,
                         VkDeviceSize size,
                         VkBufferUsageFlags usage,
                         VkBuffer &outBuffer,
                         VkDeviceMemory &outMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);

    uint32_t memoryType = findMemoryTypeIndex(requirements.memoryTypeBits, memProperties,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memoryType == UINT32_MAX) {
        memoryType = findMemoryTypeIndex(requirements.memoryTypeBits, memProperties,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    }
    if (memoryType == UINT32_MAX) {
        vkDestroyBuffer(device, buffer, nullptr);
        return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = requirements.size;
    allocInfo.memoryTypeIndex = memoryType;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        return false;
    }

    vkBindBufferMemory(device, buffer, memory, 0);
    outBuffer = buffer;
    outMemory = memory;
    return true;
}

} // namespace

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
    if (length == 0) return true;
    if (!data) return false;

    // Host-visible buffers (including device-local|host-visible UMA heaps) can be
    // mapped directly. Pure device-local buffers go through a staging copy.
    if (handle->isHostVisible) {
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
        vkFlushMappedMemoryRanges(handle->device, 1, &range);

        vkUnmapMemory(handle->device, handle->memory);
        return true;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    if (!createStagingBuffer(handle->device, handle->deviceData->memoryProperties, length,
                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             stagingBuffer, stagingMemory)) {
        return false;
    }

    void *p = nullptr;
    if (vkMapMemory(handle->device, stagingMemory, 0, length, 0, &p) != VK_SUCCESS) {
        vkFreeMemory(handle->device, stagingMemory, nullptr);
        vkDestroyBuffer(handle->device, stagingBuffer, nullptr);
        return false;
    }
    memcpy(p, data, length);
    vkUnmapMemory(handle->device, stagingMemory);

    bool success = false;
    {
        std::unique_lock<std::mutex> lock;
        if (handle->deviceData) lock = std::unique_lock<std::mutex>(handle->deviceData->gpu_mutex);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = handle->commandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(handle->device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
            vkFreeMemory(handle->device, stagingMemory, nullptr);
            vkDestroyBuffer(handle->device, stagingBuffer, nullptr);
            return false;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        VkBufferCopy region{};
        region.srcOffset = 0;
        region.dstOffset = offset;
        region.size      = length;
        vkCmdCopyBuffer(commandBuffer, stagingBuffer, handle->buffer, 1, &region);

        vkEndCommandBuffer(commandBuffer);

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(handle->device, &fenceInfo, nullptr, &fence) == VK_SUCCESS) {
            VkSubmitInfo submitInfo{};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &commandBuffer;
            if (vkQueueSubmit(handle->graphicsQueue, 1, &submitInfo, fence) == VK_SUCCESS) {
                vkWaitForFences(handle->device, 1, &fence, VK_TRUE, UINT64_MAX);
                success = true;
            }
            vkDestroyFence(handle->device, fence, nullptr);
        }

        vkFreeCommandBuffers(handle->device, handle->commandPool, 1, &commandBuffer);
    }

    vkFreeMemory(handle->device, stagingMemory, nullptr);
    vkDestroyBuffer(handle->device, stagingBuffer, nullptr);
    return success;
}

bool Buffer::download(uint64_t offset, uint64_t length, void *data) {
    auto handle = (BufferHandle *)this->native;
    if (length == 0) return true;
    if (!data) return false;

    if (handle->isHostVisible) {
        void *p;
        if (vkMapMemory(handle->device, handle->memory, offset, length, 0, &p) != VK_SUCCESS) {
            return false;
        }

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

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    if (!createStagingBuffer(handle->device, handle->deviceData->memoryProperties, length,
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             stagingBuffer, stagingMemory)) {
        return false;
    }

    bool success = false;
    {
        std::unique_lock<std::mutex> lock;
        if (handle->deviceData) lock = std::unique_lock<std::mutex>(handle->deviceData->gpu_mutex);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = handle->commandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(handle->device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
            vkFreeMemory(handle->device, stagingMemory, nullptr);
            vkDestroyBuffer(handle->device, stagingBuffer, nullptr);
            return false;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        VkBufferCopy region{};
        region.srcOffset = offset;
        region.dstOffset = 0;
        region.size      = length;
        vkCmdCopyBuffer(commandBuffer, handle->buffer, stagingBuffer, 1, &region);

        vkEndCommandBuffer(commandBuffer);

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(handle->device, &fenceInfo, nullptr, &fence) == VK_SUCCESS) {
            VkSubmitInfo submitInfo{};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &commandBuffer;
            if (vkQueueSubmit(handle->graphicsQueue, 1, &submitInfo, fence) == VK_SUCCESS) {
                vkWaitForFences(handle->device, 1, &fence, VK_TRUE, UINT64_MAX);
                success = true;
            }
            vkDestroyFence(handle->device, fence, nullptr);
        }

        vkFreeCommandBuffers(handle->device, handle->commandPool, 1, &commandBuffer);
    }

    if (success) {
        void *p = nullptr;
        if (vkMapMemory(handle->device, stagingMemory, 0, length, 0, &p) == VK_SUCCESS) {
            memcpy(data, p, length);
            vkUnmapMemory(handle->device, stagingMemory);
        } else {
            success = false;
        }
    }

    vkFreeMemory(handle->device, stagingMemory, nullptr);
    vkDestroyBuffer(handle->device, stagingBuffer, nullptr);
    return success;
}

void Buffer::downloadAsync(uint64_t offset, uint64_t length, void *data,
                           std::function<void(bool)> callback) {
    callback(download(offset, length, data));
}
