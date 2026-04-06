#include <cassert>
#include <android/log.h>
#include <algorithm>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include "command_encoder_handle.hpp"
#include "command_buffer_handle.hpp"
#include "render_pass_encoder_handle.hpp"
#include "compute_pass_encoder_handle.hpp"
#include "buffer_handle.hpp"
#include "query_set_handle.hpp"
#include "texture_handle.hpp"
#include "texture_view_handle.hpp"

using namespace systems::leal::campello_gpu;

// Extern function pointers for VK_KHR_dynamic_rendering (loaded in device.cpp)
extern PFN_vkCmdBeginRenderingKHR pfnCmdBeginRenderingKHR;

CommandEncoder::CommandEncoder(void *pd) {
    this->native = pd;

    // Begin the command buffer immediately so finish() always has a recording to end,
    // even if beginRenderPass / beginComputePass are never called.
    auto data = (CommandEncoderHandle *)pd;
    VkCommandBufferBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(data->commandBuffer, &info);

    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "CommandEncoder::CommandEncoder()");
}

CommandEncoder::~CommandEncoder() {
    auto data = (CommandEncoderHandle *)this->native;
    delete data;
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "CommandEncoder::~CommandEncoder()");
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
            __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu",
                                "beginRenderPass: vkAcquireNextImageKHR failed");
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

    if (vkEndCommandBuffer(data->commandBuffer) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu",
                            "CommandEncoder::finish: vkEndCommandBuffer failed");
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
