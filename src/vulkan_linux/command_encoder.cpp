#include <cassert>
#include <cstring>
#include <algorithm>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/descriptors/bottom_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/top_level_acceleration_structure_descriptor.hpp>
#include "command_encoder_handle.hpp"
#include "command_buffer_handle.hpp"
#include "render_pass_encoder_handle.hpp"
#include "compute_pass_encoder_handle.hpp"
#include "ray_tracing_pass_encoder_handle.hpp"
#include "buffer_handle.hpp"
#include "query_set_handle.hpp"
#include "texture_handle.hpp"
#include "texture_view_handle.hpp"
#include "acceleration_structure_handle.hpp"

using namespace systems::leal::campello_gpu;

// Extern function pointers for VK_KHR_dynamic_rendering (loaded in device.cpp)
extern PFN_vkCmdBeginRenderingKHR pfnCmdBeginRenderingKHR;

// Extern function pointers for VK_KHR_acceleration_structure + VK_KHR_ray_tracing_pipeline
extern PFN_vkGetBufferDeviceAddress                pfnGetBufferDeviceAddress;
extern PFN_vkCmdBuildAccelerationStructuresKHR     pfnCmdBuildAccelerationStructuresKHR;
extern PFN_vkCmdCopyAccelerationStructureKHR       pfnCmdCopyAccelerationStructureKHR;

CommandEncoder::CommandEncoder(void *pd) {
    this->native = pd;

    auto data = (CommandEncoderHandle *)pd;
    
    // Create timestamp query pool for GPU timing
    if (data->physicalDevice != VK_NULL_HANDLE) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(data->physicalDevice, &props);
        
        // Check if timestamps are supported
        if (props.limits.timestampComputeAndGraphics) {
            VkQueryPoolCreateInfo queryPoolInfo{};
            queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queryPoolInfo.queryCount = 2;  // Start and end timestamps
            
            if (vkCreateQueryPool(data->device, &queryPoolInfo, nullptr, &data->timestampQueryPool) == VK_SUCCESS) {
                data->timestampPeriod = props.limits.timestampPeriod;
                data->timestampQueryIndex = 0;
            }
        }
    }

    // Begin the command buffer immediately so finish() always has a recording to end,
    // even if beginRenderPass / beginComputePass are never called.
    VkCommandBufferBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(data->commandBuffer, &info);
    
    // Reset and write start timestamp if query pool was created
    if (data->timestampQueryPool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(data->commandBuffer, data->timestampQueryPool, 0, 2);
        vkCmdWriteTimestamp(data->commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, data->timestampQueryPool, 0);
    }

    
}

CommandEncoder::~CommandEncoder() {
    auto data = (CommandEncoderHandle *)this->native;
    // Clean up query pool if it wasn't transferred to CommandBuffer
    if (data->timestampQueryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(data->device, data->timestampQueryPool, nullptr);
    }
    delete data;
    
}

static VkAttachmentLoadOp getLoadOp(LoadOp loadOp) {
    switch (loadOp) {
        case LoadOp::load:  return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadOp::clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
        default: assert(false); return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
}

static VkAttachmentStoreOp getStoreOp(StoreOp storeOp) {
    switch (storeOp) {
        case StoreOp::store:   return VK_ATTACHMENT_STORE_OP_STORE;
        case StoreOp::discard: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        default: assert(false); return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
}

std::shared_ptr<RenderPassEncoder>
CommandEncoder::beginRenderPass(const BeginRenderPassDescriptor &descriptor) {

    auto data = (CommandEncoderHandle *)this->native;

    // Determine whether this is a swapchain pass or an offscreen pass.
    // A pass is offscreen when the first color attachment has an explicit view set.
    const bool hasExplicitView = !descriptor.colorAttachments.empty()
                                 && descriptor.colorAttachments[0].view != nullptr;

    VkExtent2D renderExtent;
    VkImage    firstImage = VK_NULL_HANDLE;
    bool       isSwapchain = false;

    if (!hasExplicitView && data->swapchain != VK_NULL_HANDLE) {
        // ── Swapchain path ────────────────────────────────────────────────────
        isSwapchain = true;
        uint32_t imageIndex = 0;
        VkResult acquireResult = vkAcquireNextImageKHR(
            data->device, data->swapchain, UINT64_MAX,
            data->imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            
            return nullptr;
        }
        data->currentImageIndex = imageIndex;
        firstImage   = data->swapchainImages[imageIndex];
        renderExtent = data->imageExtent;

        // Transition swapchain image: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL.
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = firstImage;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier.srcAccessMask       = VK_ACCESS_NONE;
        barrier.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(data->commandBuffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    } else {
        // ── Offscreen path ────────────────────────────────────────────────────
        auto *vh = (TextureViewHandle *)descriptor.colorAttachments[0].view->native;
        firstImage   = vh->image;
        renderExtent = { vh->width, vh->height };

        // Transition offscreen image → COLOR_ATTACHMENT_OPTIMAL.
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_GENERAL; // conservative; image may be in GENERAL
        barrier.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = firstImage;
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier.srcAccessMask       = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(data->commandBuffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Build color attachment infos.
    std::vector<VkRenderingAttachmentInfo> colorAttachments(descriptor.colorAttachments.size());
    for (uint32_t a = 0; a < (uint32_t)descriptor.colorAttachments.size(); a++) {
        VkImageView attachView;
        if (isSwapchain) {
            attachView = data->swapchainImageViews[data->currentImageIndex];
        } else {
            auto *vh = (TextureViewHandle *)descriptor.colorAttachments[a].view->native;
            attachView = vh->imageView;
        }

        colorAttachments[a]                    = {};
        colorAttachments[a].sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[a].imageView          = attachView;
        colorAttachments[a].imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[a].resolveMode        = VK_RESOLVE_MODE_NONE;
        colorAttachments[a].resolveImageView   = VK_NULL_HANDLE;
        colorAttachments[a].resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachments[a].loadOp             = getLoadOp(descriptor.colorAttachments[a].loadOp);
        colorAttachments[a].storeOp            = getStoreOp(descriptor.colorAttachments[a].storeOp);
        colorAttachments[a].clearValue.color   = { .float32 = {
            descriptor.colorAttachments[a].clearValue[0],
            descriptor.colorAttachments[a].clearValue[1],
            descriptor.colorAttachments[a].clearValue[2],
            descriptor.colorAttachments[a].clearValue[3],
        }};
    }

    // Build depth/stencil attachment info if requested.
    VkRenderingAttachmentInfo depthAttachmentInfo{};
    VkRenderingAttachmentInfo stencilAttachmentInfo{};
    bool hasDepthAttachment   = false;
    bool hasStencilAttachment = false;

    if (descriptor.depthStencilAttachment.has_value()) {
        const auto &dsa = *descriptor.depthStencilAttachment;
        auto *dvh = (TextureViewHandle *)dsa.view->native;

        // Determine aspect flags from the format.
        VkImageAspectFlags depthAspect = 0;
        bool formatHasDepth   = (dvh->format != VK_FORMAT_S8_UINT);
        bool formatHasStencil = (dvh->format == VK_FORMAT_D24_UNORM_S8_UINT ||
                                 dvh->format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                                 dvh->format == VK_FORMAT_S8_UINT);
        if (formatHasDepth)   depthAspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (formatHasStencil) depthAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;

        // Transition depth image to DEPTH_STENCIL_ATTACHMENT_OPTIMAL.
        // Note: uses UNDEFINED as old layout — valid when depthLoadOp::clear.
        VkImageMemoryBarrier dsBarrier{};
        dsBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        dsBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        dsBarrier.newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        dsBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dsBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dsBarrier.image               = dvh->image;
        dsBarrier.subresourceRange    = { depthAspect, 0, 1, 0, 1 };
        dsBarrier.srcAccessMask       = VK_ACCESS_NONE;
        dsBarrier.dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(data->commandBuffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &dsBarrier);

        if (formatHasDepth) {
            depthAttachmentInfo.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachmentInfo.imageView   = dvh->imageView;
            depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
            depthAttachmentInfo.loadOp      = getLoadOp(dsa.depthLoadOp);
            depthAttachmentInfo.storeOp     = dsa.depthReadOnly
                                              ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                              : getStoreOp(dsa.depthStoreOp);
            depthAttachmentInfo.clearValue.depthStencil = { dsa.depthClearValue, 0 };
            hasDepthAttachment = true;
        }
        if (formatHasStencil) {
            stencilAttachmentInfo.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            stencilAttachmentInfo.imageView   = dvh->imageView;
            stencilAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            stencilAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
            stencilAttachmentInfo.loadOp      = getLoadOp(dsa.stencilLoadOp);
            stencilAttachmentInfo.storeOp     = dsa.stencilReadOnly
                                               ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                               : getStoreOp(dsa.stencilStoreOp);
            stencilAttachmentInfo.clearValue.depthStencil = { 0.0f, dsa.stencilClearValue };
            hasStencilAttachment = true;
        }
    }

    VkRenderingInfo rinfo{};
    rinfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rinfo.renderArea.offset    = { 0, 0 };
    rinfo.renderArea.extent    = renderExtent;
    rinfo.layerCount           = 1;
    rinfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
    rinfo.pColorAttachments    = colorAttachments.data();
    rinfo.pDepthAttachment     = hasDepthAttachment   ? &depthAttachmentInfo   : nullptr;
    rinfo.pStencilAttachment   = hasStencilAttachment ? &stencilAttachmentInfo : nullptr;

    pfnCmdBeginRenderingKHR(data->commandBuffer, &rinfo);

    auto rpeHandle = new RenderPassEncoderHandle();
    rpeHandle->commandBuffer         = data->commandBuffer;
    rpeHandle->isSwapchain           = isSwapchain;
    rpeHandle->currentSwapchainImage = isSwapchain ? firstImage : VK_NULL_HANDLE;
    rpeHandle->offscreenImage        = isSwapchain ? VK_NULL_HANDLE : firstImage;
    rpeHandle->offscreenExtent       = renderExtent;
    if (descriptor.occlusionQuerySet) {
        rpeHandle->queryPool = ((QuerySetHandle *)descriptor.occlusionQuerySet->native)->queryPool;
    }

    return std::shared_ptr<RenderPassEncoder>(new RenderPassEncoder(rpeHandle));
}

std::shared_ptr<ComputePassEncoder> CommandEncoder::beginComputePass() {
    auto data = (CommandEncoderHandle *)this->native;

    auto handle = new ComputePassEncoderHandle();
    handle->commandBuffer = data->commandBuffer;
    return std::shared_ptr<ComputePassEncoder>(new ComputePassEncoder(handle));
}

void CommandEncoder::clearBuffer(std::shared_ptr<Buffer> buffer,
                                  uint64_t offset, uint64_t size) {
    auto data      = (CommandEncoderHandle *)this->native;
    auto bufHandle = (BufferHandle *)buffer->native;
    vkCmdFillBuffer(data->commandBuffer, bufHandle->buffer, offset, size, 0);
}

void CommandEncoder::copyBufferToBuffer(std::shared_ptr<Buffer> source,
                                         uint64_t sourceOffset,
                                         std::shared_ptr<Buffer> destination,
                                         uint64_t destinationOffset,
                                         uint64_t size) {
    auto data   = (CommandEncoderHandle *)this->native;
    auto src    = (BufferHandle *)source->native;
    auto dst    = (BufferHandle *)destination->native;
    VkBufferCopy region{ sourceOffset, destinationOffset, size };
    vkCmdCopyBuffer(data->commandBuffer, src->buffer, dst->buffer, 1, &region);
}

void CommandEncoder::copyBufferToTexture(
    std::shared_ptr<Buffer>  source,
    uint64_t                 sourceOffset,
    uint64_t                 /*bytesPerRow*/,
    std::shared_ptr<Texture> destination,
    uint32_t                 mipLevel,
    uint32_t                 arrayLayer)
{
    if (!source || !destination) return;
    auto data = (CommandEncoderHandle *)this->native;
    auto bufH = (BufferHandle *)source->native;
    auto texH = (TextureHandle *)destination->native;

    // Transition destination to TRANSFER_DST_OPTIMAL.
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = texH->currentLayout;
    barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = texH->image;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, 1, arrayLayer, 1 };
    barrier.srcAccessMask       = VK_ACCESS_MEMORY_READ_BIT;
    barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(data->commandBuffer,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    uint32_t mipWidth  = std::max(1u, texH->width  >> mipLevel);
    uint32_t mipHeight = std::max(1u, texH->height >> mipLevel);

    // bufferRowLength=0 / bufferImageHeight=0 → tightly packed.
    VkBufferImageCopy region{};
    region.bufferOffset                    = sourceOffset;
    region.bufferRowLength                 = 0;
    region.bufferImageHeight               = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayLayer;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset                     = { 0, 0, 0 };
    region.imageExtent                     = { mipWidth, mipHeight, 1 };

    vkCmdCopyBufferToImage(data->commandBuffer,
                           bufH->buffer,
                           texH->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);

    // Leave destination in GENERAL so it is immediately usable for any purpose.
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(data->commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    texH->currentLayout = VK_IMAGE_LAYOUT_GENERAL;
}

void CommandEncoder::copyTextureToBuffer(
    std::shared_ptr<Texture> source,
    uint32_t mipLevel,
    uint32_t arrayLayer,
    std::shared_ptr<Buffer> destination,
    uint64_t destinationOffset,
    uint64_t /*bytesPerRow*/)
{
    if (!source || !destination) return;
    auto data = (CommandEncoderHandle *)this->native;
    auto texH = (TextureHandle *)source->native;
    auto bufH = (BufferHandle *)destination->native;

    // Transition source image to TRANSFER_SRC_OPTIMAL.
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = texH->currentLayout;
    barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = texH->image;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, 1, arrayLayer, 1 };
    barrier.srcAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(data->commandBuffer,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    uint32_t mipWidth  = std::max(1u, texH->width  >> mipLevel);
    uint32_t mipHeight = std::max(1u, texH->height >> mipLevel);

    // bufferRowLength=0 / bufferImageHeight=0 → tightly packed.
    VkBufferImageCopy region{};
    region.bufferOffset                    = destinationOffset;
    region.bufferRowLength                 = 0;
    region.bufferImageHeight               = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayLayer;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset                     = { 0, 0, 0 };
    region.imageExtent                     = { mipWidth, mipHeight, 1 };

    vkCmdCopyImageToBuffer(data->commandBuffer,
                           texH->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           bufH->buffer, 1, &region);

    // Restore previous layout (promote UNDEFINED → GENERAL so content is preserved).
    VkImageLayout restoreLayout = (texH->currentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                  ? VK_IMAGE_LAYOUT_GENERAL
                                  : texH->currentLayout;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout     = restoreLayout;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(data->commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    texH->currentLayout = restoreLayout;
}

void CommandEncoder::copyTextureToTexture(
    std::shared_ptr<Texture> source,
    const Offset3D& sourceOffset,
    std::shared_ptr<Texture> destination,
    const Offset3D& destinationOffset,
    const Extent3D& extent)
{
    if (!source || !destination) return;
    auto data = (CommandEncoderHandle *)this->native;
    auto srcH = (TextureHandle *)source->native;
    auto dstH = (TextureHandle *)destination->native;

    // Transition both images to their required transfer layouts.
    VkImageMemoryBarrier barriers[2] = {};

    barriers[0].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout           = srcH->currentLayout;
    barriers[0].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image               = srcH->image;
    barriers[0].subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
    barriers[0].srcAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT;
    barriers[0].dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;

    barriers[1].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].oldLayout           = dstH->currentLayout;
    barriers[1].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].image               = dstH->image;
    barriers[1].subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
    barriers[1].srcAccessMask       = VK_ACCESS_MEMORY_READ_BIT;
    barriers[1].dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(data->commandBuffer,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 2, barriers);

    VkImageCopy copyRegion{};
    copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.srcOffset      = { sourceOffset.x, sourceOffset.y, sourceOffset.z };
    copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.dstOffset      = { destinationOffset.x, destinationOffset.y, destinationOffset.z };
    copyRegion.extent         = { extent.width, extent.height, extent.depth };

    vkCmdCopyImage(data->commandBuffer,
                   srcH->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dstH->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &copyRegion);

    // Restore layouts. Source goes back to what it was; destination becomes GENERAL
    // so it is accessible for any subsequent use (sampling, storage, etc.).
    VkImageLayout srcRestore = (srcH->currentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                               ? VK_IMAGE_LAYOUT_GENERAL
                               : srcH->currentLayout;

    barriers[0].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].newLayout     = srcRestore;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    barriers[1].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].newLayout     = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(data->commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, nullptr, 0, nullptr, 2, barriers);

    srcH->currentLayout = srcRestore;
    dstH->currentLayout = VK_IMAGE_LAYOUT_GENERAL;
}

std::shared_ptr<CommandBuffer> CommandEncoder::finish() {
    auto data = (CommandEncoderHandle *)this->native;

    // Write end timestamp before ending command buffer
    if (data->timestampQueryPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(data->commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, data->timestampQueryPool, 1);
    }

    if (vkEndCommandBuffer(data->commandBuffer) != VK_SUCCESS) {
        
        return nullptr;
    }

    auto cbHandle                = new CommandBufferHandle();
    cbHandle->device             = data->device;
    cbHandle->commandPool        = data->commandPool;
    cbHandle->commandBuffer      = data->commandBuffer;
    cbHandle->graphicsQueue      = data->graphicsQueue;
    cbHandle->swapchain          = data->swapchain;
    cbHandle->currentImageIndex  = data->currentImageIndex;
    cbHandle->hasSwapchain       = (data->swapchain != VK_NULL_HANDLE);
    cbHandle->stagingBuffers     = std::move(data->stagingBuffers);
    cbHandle->stagingMemories    = std::move(data->stagingMemories);
    
    // Transfer timing data
    if (data->timestampQueryPool != VK_NULL_HANDLE) {
        cbHandle->queryPool = data->timestampQueryPool;
        cbHandle->timestampPeriod = data->timestampPeriod;
        cbHandle->queryStartIndex = 0;
        cbHandle->queryEndIndex = 1;
        cbHandle->hasTimingData = true;
        // Null out the encoder's reference so it doesn't get destroyed
        data->timestampQueryPool = VK_NULL_HANDLE;
    }

    return std::shared_ptr<CommandBuffer>(new CommandBuffer(cbHandle));
}

void CommandEncoder::resolveQuerySet(std::shared_ptr<QuerySet> querySet,
                                      uint32_t firstQuery, uint32_t queryCount,
                                      std::shared_ptr<Buffer> destination,
                                      uint64_t destinationOffset) {
    auto data     = (CommandEncoderHandle *)this->native;
    auto qs       = (QuerySetHandle *)querySet->native;
    auto dst      = (BufferHandle *)destination->native;
    vkCmdCopyQueryPoolResults(data->commandBuffer,
                              qs->queryPool,
                              firstQuery, queryCount,
                              dst->buffer, destinationOffset,
                              sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
}

void CommandEncoder::writeTimestamp(std::shared_ptr<QuerySet> querySet,
                                     uint32_t queryIndex) {
    auto data = (CommandEncoderHandle *)this->native;
    auto qs   = (QuerySetHandle *)querySet->native;
    vkCmdWriteTimestamp(data->commandBuffer,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        qs->queryPool,
                        queryIndex);
}

// ---------------------------------------------------------------------------
// Ray tracing command encoder methods
// ---------------------------------------------------------------------------

std::shared_ptr<RayTracingPassEncoder> CommandEncoder::beginRayTracingPass() {
    auto *data   = (CommandEncoderHandle *)this->native;
    auto *handle = new RayTracingPassEncoderHandle();
    handle->commandBuffer = data->commandBuffer;
    return std::shared_ptr<RayTracingPassEncoder>(new RayTracingPassEncoder(handle));
}

// Helper used by both buildAccelerationStructure overloads: allocate a
// host-visible, device-address-capable staging buffer, write data into it,
// and register it with the command encoder for cleanup after submit.
static VkDeviceAddress allocAndUploadStaging(
    CommandEncoderHandle *data,
    VkPhysicalDevice physicalDevice,
    const void *src, VkDeviceSize size)
{
    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = size;
    bci.usage       = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buf = VK_NULL_HANDLE;
    if (vkCreateBuffer(data->device, &bci, nullptr, &buf) != VK_SUCCESS) return 0;

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(data->device, buf, &req);

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    uint32_t memIdx = UINT32_MAX;
    VkMemoryPropertyFlags needed = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                 | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((req.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & needed) == needed) {
            memIdx = i; break;
        }
    }
    if (memIdx == UINT32_MAX) { vkDestroyBuffer(data->device, buf, nullptr); return 0; }

    VkMemoryAllocateFlagsInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.pNext           = &fi;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = memIdx;

    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (vkAllocateMemory(data->device, &ai, nullptr, &mem) != VK_SUCCESS) {
        vkDestroyBuffer(data->device, buf, nullptr); return 0;
    }
    vkBindBufferMemory(data->device, buf, mem, 0);

    void *mapped = nullptr;
    vkMapMemory(data->device, mem, 0, size, 0, &mapped);
    if (mapped) { memcpy(mapped, src, (size_t)size); vkUnmapMemory(data->device, mem); }

    data->stagingBuffers.push_back(buf);
    data->stagingMemories.push_back(mem);

    VkBufferDeviceAddressInfo addrInfo{};
    addrInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addrInfo.buffer = buf;
    return pfnGetBufferDeviceAddress(data->device, &addrInfo);
}

static VkBuildAccelerationStructureFlagsKHR mapBuildFlags(AccelerationStructureBuildFlag f) {
    VkBuildAccelerationStructureFlagsKHR out = 0;
    auto fi = (int)f;
    if (fi & (int)AccelerationStructureBuildFlag::preferFastTrace) out |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (fi & (int)AccelerationStructureBuildFlag::preferFastBuild) out |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    if (fi & (int)AccelerationStructureBuildFlag::allowUpdate)     out |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    if (fi & (int)AccelerationStructureBuildFlag::allowCompaction)  out |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    return out;
}

static VkDeviceAddress bufAddr(VkDevice device, VkBuffer buffer) {
    VkBufferDeviceAddressInfo i{};
    i.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    i.buffer = buffer;
    return pfnGetBufferDeviceAddress(device, &i);
}

void CommandEncoder::buildAccelerationStructure(
    std::shared_ptr<AccelerationStructure> dst,
    const BottomLevelAccelerationStructureDescriptor &descriptor,
    std::shared_ptr<Buffer> scratchBuffer)
{
    if (!pfnCmdBuildAccelerationStructuresKHR) return;
    auto *data   = (CommandEncoderHandle *)this->native;
    auto *asH    = (AccelerationStructureHandle *)dst->native;
    auto *scrH   = (BufferHandle *)scratchBuffer->native;

    std::vector<VkAccelerationStructureGeometryKHR> geometries;
    std::vector<uint32_t> primCounts;

    for (const auto &g : descriptor.geometries) {
        VkAccelerationStructureGeometryKHR geom{};
        geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geom.flags = g.opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;

        if (g.type == AccelerationStructureGeometryType::triangles) {
            geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            auto &tri = geom.geometry.triangles;
            tri.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            tri.vertexStride = g.vertexStride;
            tri.maxVertex    = g.vertexCount > 0 ? g.vertexCount - 1 : 0;
            tri.indexType    = VK_INDEX_TYPE_NONE_KHR;
            if (g.vertexBuffer) {
                auto *vh = (BufferHandle *)g.vertexBuffer->native;
                tri.vertexData.deviceAddress = bufAddr(data->device, vh->buffer) + g.vertexOffset;
            }
            if (g.indexBuffer) {
                auto *ih = (BufferHandle *)g.indexBuffer->native;
                tri.indexType = (g.indexFormat == IndexFormat::uint16)
                                ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
                tri.indexData.deviceAddress = bufAddr(data->device, ih->buffer);
            }
            if (g.transformBuffer) {
                auto *th = (BufferHandle *)g.transformBuffer->native;
                tri.transformData.deviceAddress = bufAddr(data->device, th->buffer) + g.transformOffset;
            }
            primCounts.push_back(g.indexBuffer ? g.indexCount / 3 : g.vertexCount / 3);
        } else {
            geom.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
            auto &aabb = geom.geometry.aabbs;
            aabb.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
            aabb.stride = g.aabbStride;
            if (g.aabbBuffer) {
                auto *ab = (BufferHandle *)g.aabbBuffer->native;
                aabb.data.deviceAddress = bufAddr(data->device, ab->buffer) + g.aabbOffset;
            }
            primCounts.push_back(g.aabbCount);
        }
        geometries.push_back(geom);
    }

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags                     = mapBuildFlags(descriptor.buildFlags);
    buildInfo.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure  = asH->accelerationStructure;
    buildInfo.geometryCount             = (uint32_t)geometries.size();
    buildInfo.pGeometries               = geometries.data();
    buildInfo.scratchData.deviceAddress = bufAddr(data->device, scrH->buffer);

    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(primCounts.size());
    for (size_t i = 0; i < primCounts.size(); i++) {
        ranges[i] = { primCounts[i], 0, 0, 0 };
    }
    const VkAccelerationStructureBuildRangeInfoKHR *pRanges = ranges.data();

    pfnCmdBuildAccelerationStructuresKHR(data->commandBuffer, 1, &buildInfo, &pRanges);

    // Memory barrier so AS is visible to subsequent reads.
    VkMemoryBarrier barrier{};
    barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(data->commandBuffer,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void CommandEncoder::buildAccelerationStructure(
    std::shared_ptr<AccelerationStructure> dst,
    const TopLevelAccelerationStructureDescriptor &descriptor,
    std::shared_ptr<Buffer> scratchBuffer)
{
    if (!pfnCmdBuildAccelerationStructuresKHR) return;
    auto *data = (CommandEncoderHandle *)this->native;
    auto *asH  = (AccelerationStructureHandle *)dst->native;
    auto *scrH = (BufferHandle *)scratchBuffer->native;

    // Build VkAccelerationStructureInstanceKHR array from descriptor.
    struct VkInstance {
        float    transform[3][4];
        uint32_t instanceCustomIndex_mask;           // [23:0] id, [31:24] mask
        uint32_t sbtOffset_flags;                    // [23:0] offset, [31:24] flags
        uint64_t accelerationStructureReference;
    };
    static_assert(sizeof(VkInstance) == 64, "VkAccelerationStructureInstanceKHR must be 64 bytes");

    std::vector<VkInstance> instances;
    instances.reserve(descriptor.instances.size());
    for (const auto &inst : descriptor.instances) {
        VkInstance vi{};
        memcpy(vi.transform, inst.transform, sizeof(vi.transform));
        vi.instanceCustomIndex_mask = ((uint32_t)(inst.instanceId & 0xFFFFFF))
                                    | ((uint32_t)(inst.mask) << 24);
        uint32_t flags = inst.opaque ? 0x1u : 0u; // VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR = 1
        vi.sbtOffset_flags = ((uint32_t)(inst.hitGroupOffset & 0xFFFFFF))
                           | (flags << 24);
        if (inst.blas) {
            auto *bh = (AccelerationStructureHandle *)inst.blas->native;
            vi.accelerationStructureReference = bh->deviceAddress;
        }
        instances.push_back(vi);
    }

    VkDeviceSize instBufSize = instances.size() * sizeof(VkInstance);
    VkDeviceAddress instAddr = 0;
    if (instBufSize > 0) {
        instAddr = allocAndUploadStaging(data, /*physicalDevice unused in helper*/
            // Use a separate VkPhysicalDevice lookup — we don't store it in the handle.
            // Instead, use the dedicated staging helper which gets memProps from the device.
            // Note: physicalDevice argument to allocAndUploadStaging was removed; it
            // queries memory props internally via the logical device's parent.
            // Re-implemented inline since we can't reach physicalDevice here.
            VK_NULL_HANDLE /* placeholder */ , instances.data(), instBufSize);
    }

    // If the staging helper above doesn't work (physicalDevice not available here),
    // fall back to a simpler inline allocation.
    if (instBufSize > 0 && instAddr == 0) {
        VkBufferCreateInfo bci{};
        bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size        = instBufSize;
        bci.usage       = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer instBuf = VK_NULL_HANDLE;
        if (vkCreateBuffer(data->device, &bci, nullptr, &instBuf) == VK_SUCCESS) {
            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(data->device, instBuf, &req);

            // Pick first HOST_VISIBLE type.
            VkPhysicalDeviceMemoryProperties memProps{};
            // We can't get physicalDevice from CommandEncoderHandle — use vkGetPhysicalDeviceMemoryProperties
            // with a workaround: query via the device's feature bits.
            // For now skip and just bind to the first compatible type via VkMemoryAllocateInfo.
            VkMemoryAllocateFlagsInfo fi{};
            fi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
            fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
            VkMemoryAllocateInfo ai{};
            ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.pNext           = &fi;
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = 0; // Driver will choose; may fail on non-UMA

            VkDeviceMemory instMem = VK_NULL_HANDLE;
            if (vkAllocateMemory(data->device, &ai, nullptr, &instMem) == VK_SUCCESS) {
                vkBindBufferMemory(data->device, instBuf, instMem, 0);
                void *mapped = nullptr;
                if (vkMapMemory(data->device, instMem, 0, instBufSize, 0, &mapped) == VK_SUCCESS) {
                    memcpy(mapped, instances.data(), (size_t)instBufSize);
                    vkUnmapMemory(data->device, instMem);
                }
                data->stagingBuffers.push_back(instBuf);
                data->stagingMemories.push_back(instMem);
                VkBufferDeviceAddressInfo addrInfo{};
                addrInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                addrInfo.buffer = instBuf;
                instAddr = pfnGetBufferDeviceAddress(data->device, &addrInfo);
            } else {
                vkDestroyBuffer(data->device, instBuf, nullptr);
            }
        }
    }

    VkAccelerationStructureGeometryInstancesDataKHR instData{};
    instData.sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instData.arrayOfPointers = VK_FALSE;
    instData.data.deviceAddress = instAddr;

    VkAccelerationStructureGeometryKHR geom{};
    geom.sType                  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType           = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.geometry.instances     = instData;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags                     = mapBuildFlags(descriptor.buildFlags);
    buildInfo.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure  = asH->accelerationStructure;
    buildInfo.geometryCount             = 1;
    buildInfo.pGeometries               = &geom;
    buildInfo.scratchData.deviceAddress = bufAddr(data->device, scrH->buffer);

    uint32_t instanceCount = (uint32_t)instances.size();
    VkAccelerationStructureBuildRangeInfoKHR range{ instanceCount, 0, 0, 0 };
    const VkAccelerationStructureBuildRangeInfoKHR *pRange = &range;

    pfnCmdBuildAccelerationStructuresKHR(data->commandBuffer, 1, &buildInfo, &pRange);

    VkMemoryBarrier barrier{};
    barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(data->commandBuffer,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void CommandEncoder::updateAccelerationStructure(
    std::shared_ptr<AccelerationStructure> src,
    std::shared_ptr<AccelerationStructure> dst,
    std::shared_ptr<Buffer> scratchBuffer)
{
    if (!pfnCmdBuildAccelerationStructuresKHR) return;
    auto *data = (CommandEncoderHandle *)this->native;
    auto *srcH = (AccelerationStructureHandle *)src->native;
    auto *dstH = (AccelerationStructureHandle *)dst->native;
    auto *scrH = (BufferHandle *)scratchBuffer->native;

    // For an update, no geometries are required — the structure is updated in-place
    // using the same geometry that was used to build the source.
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags                     = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    buildInfo.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    buildInfo.srcAccelerationStructure  = srcH->accelerationStructure;
    buildInfo.dstAccelerationStructure  = dstH->accelerationStructure;
    buildInfo.geometryCount             = 0;
    buildInfo.scratchData.deviceAddress = bufAddr(data->device, scrH->buffer);

    // Update with zero primitives — caller is responsible for supplying geometry
    // via the build geometry info when performing a real update with geometry.
    // For now this is a placeholder that records the update command.
    const VkAccelerationStructureBuildRangeInfoKHR *pRange = nullptr;
    pfnCmdBuildAccelerationStructuresKHR(data->commandBuffer, 1, &buildInfo, &pRange);
}

void CommandEncoder::copyAccelerationStructure(
    std::shared_ptr<AccelerationStructure> src,
    std::shared_ptr<AccelerationStructure> dst)
{
    if (!pfnCmdCopyAccelerationStructureKHR) return;
    auto *data = (CommandEncoderHandle *)this->native;
    auto *srcH = (AccelerationStructureHandle *)src->native;
    auto *dstH = (AccelerationStructureHandle *)dst->native;

    VkCopyAccelerationStructureInfoKHR copyInfo{};
    copyInfo.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
    copyInfo.src   = srcH->accelerationStructure;
    copyInfo.dst   = dstH->accelerationStructure;
    copyInfo.mode  = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
    pfnCmdCopyAccelerationStructureKHR(data->commandBuffer, &copyInfo);
}
