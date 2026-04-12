#include <algorithm>
#include <vector>
#include <android/log.h>
#include <vulkan/vulkan.h>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include "texture_handle.hpp"
#include "texture_view_handle.hpp"
#include "buffer_handle.hpp"
#include "common.hpp"

using namespace systems::leal::campello_gpu;

// Forward-declared; defined in device.cpp.
VkFormat pixelFormatToNative(systems::leal::campello_gpu::PixelFormat format);

Texture::Texture(void *pd) {
    this->native = pd;
}

Texture::~Texture() {
    auto handle = (TextureHandle *)native;
    
    // Phase 2: Update memory tracking
    if (handle->deviceData) {
        handle->deviceData->textureCount--;
        handle->deviceData->textureBytes.fetch_sub(handle->allocatedSize);
    }
    
    handle->buffer = nullptr;
    if (handle->defaultView != VK_NULL_HANDLE) {
        vkDestroyImageView(handle->device, handle->defaultView, nullptr);
    }
    vkDestroyImage(handle->device, handle->image, nullptr);
    delete handle;
}

bool Texture::upload(uint64_t offset, uint64_t length, void *data) {
    auto handle = (TextureHandle *)native;

    // Write data into the host-visible staging buffer.
    if (!handle->buffer->upload(offset, length, data)) return false;

    // Now copy from the staging buffer into the VkImage via a one-shot command buffer.
    VkCommandBufferAllocateInfo cbAllocInfo{};
    cbAllocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAllocInfo.commandPool        = handle->commandPool;
    cbAllocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(handle->device, &cbAllocInfo, &cmd) != VK_SUCCESS)
        return false;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Transition image → TRANSFER_DST_OPTIMAL.
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = handle->currentLayout;
    barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = handle->image;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, handle->mipLevels, 0, 1 };
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Build one VkBufferImageCopy per mip level covered by [offset, offset+length).
    std::vector<VkBufferImageCopy> regions;
    uint64_t bufOffset = offset;
    uint32_t w = handle->width;
    uint32_t h = handle->height;
    for (uint32_t mip = 0; mip < handle->mipLevels; mip++) {
        uint64_t mipSize = (uint64_t)w * h * handle->depth * handle->samples
                           * getPixelFormatSize(handle->pixelFormat) / 8;
        if (bufOffset + mipSize > offset + length) break; // stop when we run past uploaded data

        VkBufferImageCopy region{};
        region.bufferOffset                    = bufOffset;
        region.bufferRowLength                 = 0; // tightly packed
        region.bufferImageHeight               = 0; // tightly packed
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = mip;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = { 0, 0, 0 };
        region.imageExtent                     = { w, h, handle->depth };
        regions.push_back(region);

        bufOffset += mipSize;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    if (!regions.empty()) {
        auto bufH = (BufferHandle *)handle->buffer->native;
        vkCmdCopyBufferToImage(cmd, bufH->buffer, handle->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(regions.size()), regions.data());
    }

    // Transition → SHADER_READ_ONLY_OPTIMAL so the image is ready for sampling.
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    handle->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vkEndCommandBuffer(cmd);

    // Submit and wait synchronously.
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    vkCreateFence(handle->device, &fenceInfo, nullptr, &fence);
    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(handle->graphicsQueue, 1, &submitInfo, fence);
    vkWaitForFences(handle->device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(handle->device, fence, nullptr);
    vkFreeCommandBuffers(handle->device, handle->commandPool, 1, &cmd);

    return true;
}

std::shared_ptr<TextureView> Texture::createView(PixelFormat format,
                                                   uint32_t    arrayLayerCount,
                                                   Aspect      aspect,
                                                   uint32_t    baseArrayLayer,
                                                   uint32_t    baseMipLevel,
                                                   TextureType dimension) {
    auto handle = (TextureHandle *)native;

    VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
    switch (aspect) {
        case Aspect::depthOnly:   aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;   break;
        case Aspect::stencilOnly: aspectFlags = VK_IMAGE_ASPECT_STENCIL_BIT; break;
        default: break;
    }

    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    switch (dimension) {
        case TextureType::tt1d: viewType = VK_IMAGE_VIEW_TYPE_1D; break;
        case TextureType::tt3d: viewType = VK_IMAGE_VIEW_TYPE_3D; break;
        default: break;
    }

    uint32_t layerCount = (arrayLayerCount == static_cast<uint32_t>(-1))
                              ? VK_REMAINING_ARRAY_LAYERS
                              : arrayLayerCount;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = handle->image;
    viewInfo.viewType = viewType;
    viewInfo.format   = pixelFormatToNative(format);
    viewInfo.subresourceRange.aspectMask     = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel   = baseMipLevel;
    viewInfo.subresourceRange.levelCount     = VK_REMAINING_MIP_LEVELS;
    viewInfo.subresourceRange.baseArrayLayer = baseArrayLayer;
    viewInfo.subresourceRange.layerCount     = layerCount;

    VkImageView imageView = VK_NULL_HANDLE;
    if (vkCreateImageView(handle->device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        return nullptr;
    }

    uint32_t mipW = std::max(1u, handle->width  >> baseMipLevel);
    uint32_t mipH = std::max(1u, handle->height >> baseMipLevel);

    auto vh = new TextureViewHandle();
    vh->device    = handle->device;
    vh->imageView = imageView;
    vh->image     = handle->image;
    vh->format    = viewInfo.format;
    vh->width     = mipW;
    vh->height    = mipH;
    vh->owned     = true;
    return std::shared_ptr<TextureView>(new TextureView(vh));
}

uint32_t Texture::getDepthOrarrayLayers() {
    return ((TextureHandle *)native)->depth;
}

TextureType Texture::getDimension() {
    return ((TextureHandle *)native)->textureType;
}

PixelFormat Texture::getFormat() {
    return ((TextureHandle *)native)->pixelFormat;
}

uint32_t Texture::getWidth() {
    return ((TextureHandle *)native)->width;
}

uint32_t Texture::getHeight() {
    return ((TextureHandle *)native)->height;
}

uint32_t Texture::getMipLevelCount() {
    return ((TextureHandle *)native)->mipLevels;
}

uint32_t Texture::getSampleCount() {
    return ((TextureHandle *)native)->samples;
}

TextureUsage Texture::getUsage() {
    return ((TextureHandle *)native)->usage;
}

bool Texture::download(uint32_t mipLevel, uint32_t arrayLayer, void *data, uint64_t length) {
    if (!native || !data || length == 0) return false;
    auto handle = (TextureHandle *)native;

    // Allocate a host-visible readback buffer.
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size        = length;
    bufInfo.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer readbackBuffer;
    if (vkCreateBuffer(handle->device, &bufInfo, nullptr, &readbackBuffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(handle->device, readbackBuffer, &memReq);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(handle->physicalDevice, &memProps);

    uint32_t memTypeIndex = UINT32_MAX;
    VkMemoryPropertyFlags needed = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReq.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & needed) == needed) {
            memTypeIndex = i;
            break;
        }
    }
    if (memTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(handle->device, readbackBuffer, nullptr);
        return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;
    VkDeviceMemory readbackMemory;
    if (vkAllocateMemory(handle->device, &allocInfo, nullptr, &readbackMemory) != VK_SUCCESS) {
        vkDestroyBuffer(handle->device, readbackBuffer, nullptr);
        return false;
    }
    vkBindBufferMemory(handle->device, readbackBuffer, readbackMemory, 0);

    // Allocate a one-shot command buffer.
    VkCommandBufferAllocateInfo cbAllocInfo{};
    cbAllocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAllocInfo.commandPool        = handle->commandPool;
    cbAllocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(handle->device, &cbAllocInfo, &cmd) != VK_SUCCESS) {
        vkFreeMemory(handle->device, readbackMemory, nullptr);
        vkDestroyBuffer(handle->device, readbackBuffer, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Transition image to TRANSFER_SRC_OPTIMAL.
    VkImageLayout oldLayout = handle->currentLayout;
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = handle->image;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, 1, arrayLayer, 1 };
    barrier.srcAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    uint32_t mipWidth  = std::max(1u, handle->width  >> mipLevel);
    uint32_t mipHeight = std::max(1u, handle->height >> mipLevel);

    VkBufferImageCopy region{};
    region.bufferOffset                    = 0;
    region.bufferRowLength                 = 0;  // tightly packed
    region.bufferImageHeight               = 0;  // tightly packed
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayLayer;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset                     = { 0, 0, 0 };
    region.imageExtent                     = { mipWidth, mipHeight, 1 };
    vkCmdCopyImageToBuffer(cmd, handle->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuffer, 1, &region);

    // Restore to previous layout (promote UNDEFINED → GENERAL).
    VkImageLayout restoreLayout = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                  ? VK_IMAGE_LAYOUT_GENERAL
                                  : oldLayout;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout     = restoreLayout;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    handle->currentLayout = restoreLayout;

    vkEndCommandBuffer(cmd);

    // Submit and wait synchronously.
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    vkCreateFence(handle->device, &fenceInfo, nullptr, &fence);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(handle->graphicsQueue, 1, &submitInfo, fence);
    vkWaitForFences(handle->device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(handle->device, fence, nullptr);

    // Read back.
    void *p;
    vkMapMemory(handle->device, readbackMemory, 0, length, 0, &p);
    memcpy(data, p, length);
    vkUnmapMemory(handle->device, readbackMemory);

    // Cleanup.
    vkFreeCommandBuffers(handle->device, handle->commandPool, 1, &cmd);
    vkFreeMemory(handle->device, readbackMemory, nullptr);
    vkDestroyBuffer(handle->device, readbackBuffer, nullptr);

    return true;
}
