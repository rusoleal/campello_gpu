#include <cassert>
#include <android/log.h>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/buffer.hpp>
#include "command_encoder_handle.hpp"
#include "command_buffer_handle.hpp"
#include "render_pass_encoder_handle.hpp"
#include "compute_pass_encoder_handle.hpp"
#include "buffer_handle.hpp"
#include "query_set_handle.hpp"

using namespace systems::leal::campello_gpu;

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

    // Acquire the next swapchain image.
    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        data->device,
        data->swapchain,
        UINT64_MAX,
        data->imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex);

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu",
                            "beginRenderPass: vkAcquireNextImageKHR failed");
        return nullptr;
    }
    data->currentImageIndex = imageIndex;

    // Transition swapchain image: undefined → color attachment optimal.
    VkImage currentImage = data->swapchainImages[imageIndex];
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = currentImage;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask       = VK_ACCESS_NONE;
    barrier.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(data->commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Build color attachment infos.
    std::vector<VkRenderingAttachmentInfo> colorAttachments(descriptor.colorAttachments.size());
    for (uint32_t a = 0; a < descriptor.colorAttachments.size(); a++) {
        colorAttachments[a]                  = {};
        colorAttachments[a].sType            = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[a].imageView        = data->swapchainImageViews[imageIndex];
        colorAttachments[a].imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[a].resolveMode      = VK_RESOLVE_MODE_NONE;
        colorAttachments[a].resolveImageView = VK_NULL_HANDLE;
        colorAttachments[a].resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachments[a].loadOp           = getLoadOp(descriptor.colorAttachments[a].loadOp);
        colorAttachments[a].storeOp          = getStoreOp(descriptor.colorAttachments[a].storeOp);
        colorAttachments[a].clearValue.color = {
            .float32 = {
                descriptor.colorAttachments[a].clearValue[0],
                descriptor.colorAttachments[a].clearValue[1],
                descriptor.colorAttachments[a].clearValue[2],
                descriptor.colorAttachments[a].clearValue[3],
            }
        };
    }

    VkRenderingInfo rinfo{};
    rinfo.sType                  = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rinfo.renderArea.offset      = { 0, 0 };
    rinfo.renderArea.extent      = data->imageExtent;
    rinfo.layerCount             = 1;
    rinfo.colorAttachmentCount   = static_cast<uint32_t>(colorAttachments.size());
    rinfo.pColorAttachments      = colorAttachments.data();
    rinfo.pDepthAttachment       = VK_NULL_HANDLE;
    rinfo.pStencilAttachment     = VK_NULL_HANDLE;

    vkCmdBeginRendering(data->commandBuffer, &rinfo);

    auto rpeHandle = new RenderPassEncoderHandle();
    rpeHandle->commandBuffer        = data->commandBuffer;
    rpeHandle->currentSwapchainImage = currentImage;

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

void CommandEncoder::copyBufferToTexture() {
    // TODO: signature needs source/destination descriptors; left unimplemented.
}

void CommandEncoder::copyTextureToBuffer() {
    // TODO: signature needs source/destination descriptors; left unimplemented.
}

void CommandEncoder::copyTextureToTexture() {
    // TODO: signature needs source/destination descriptors; left unimplemented.
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
