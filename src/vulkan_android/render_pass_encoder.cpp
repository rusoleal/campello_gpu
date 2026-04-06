#include <android/log.h>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include "render_pass_encoder_handle.hpp"
#include "buffer_handle.hpp"
#include "render_pipeline_handle.hpp"

using namespace systems::leal::campello_gpu;

// Extern function pointer for VK_KHR_dynamic_rendering (loaded in device.cpp)
extern PFN_vkCmdEndRenderingKHR pfnCmdEndRenderingKHR;

RenderPassEncoder::RenderPassEncoder(void *pd) {
    native = pd;
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "RenderPassEncoder::RenderPassEncoder()");
}

RenderPassEncoder::~RenderPassEncoder() {
    auto data = (RenderPassEncoderHandle *)native;
    delete data;
}

void RenderPassEncoder::beginOcclusionQuery(uint32_t queryIndex) {
    // TODO: requires a VkQueryPool reference stored in the handle.
}

void RenderPassEncoder::draw(uint32_t vertexCount, uint32_t instanceCount,
                              uint32_t firstVertex, uint32_t firstInstance) {
    auto data = (RenderPassEncoderHandle *)native;
    vkCmdDraw(data->commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void RenderPassEncoder::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                     uint32_t firstVertex, uint32_t baseVertex,
                                     uint32_t firstInstance) {
    auto data = (RenderPassEncoderHandle *)native;
    vkCmdDrawIndexed(data->commandBuffer, indexCount, instanceCount,
                     firstVertex, static_cast<int32_t>(baseVertex), firstInstance);
}

void RenderPassEncoder::drawIndirect(std::shared_ptr<Buffer> indirectBuffer,
                                      uint64_t indirectOffset) {
    auto data      = (RenderPassEncoderHandle *)native;
    auto bufHandle = (BufferHandle *)indirectBuffer->native;
    // drawCount=1, stride=sizeof(VkDrawIndirectCommand)=16
    vkCmdDrawIndirect(data->commandBuffer, bufHandle->buffer, indirectOffset, 1, 16);
}

void RenderPassEncoder::drawIndexedIndirect(std::shared_ptr<Buffer> indirectBuffer,
                                             uint64_t indirectOffset) {
    auto data      = (RenderPassEncoderHandle *)native;
    auto bufHandle = (BufferHandle *)indirectBuffer->native;
    // drawCount=1, stride=sizeof(VkDrawIndexedIndirectCommand)=20
    vkCmdDrawIndexedIndirect(data->commandBuffer, bufHandle->buffer, indirectOffset, 1, 20);
}

void RenderPassEncoder::end() {
    auto data = (RenderPassEncoderHandle *)native;

    pfnCmdEndRenderingKHR(data->commandBuffer);

    // Transition the swapchain image from color attachment to present layout.
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = data->currentSwapchainImage;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask       = VK_ACCESS_NONE;

    vkCmdPipelineBarrier(data->commandBuffer,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void RenderPassEncoder::endOcclusionQuery() {
    // TODO: requires a VkQueryPool reference stored in the handle.
}

void RenderPassEncoder::setIndexBuffer(std::shared_ptr<Buffer> buffer,
                                        IndexFormat indexFormat,
                                        uint64_t offset, int64_t size) {
    auto data      = (RenderPassEncoderHandle *)native;
    auto bufHandle = (BufferHandle *)buffer->native;
    VkIndexType vkType = (indexFormat == IndexFormat::uint16)
                             ? VK_INDEX_TYPE_UINT16
                             : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(data->commandBuffer, bufHandle->buffer, offset, vkType);
}

void RenderPassEncoder::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
    auto data = (RenderPassEncoderHandle *)native;
    auto pipe = (RenderPipelineHandle *)pipeline->native;
    vkCmdBindPipeline(data->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->pipeline);
}

void RenderPassEncoder::setScissorRect(float x, float y, float width, float height) {
    auto data = (RenderPassEncoderHandle *)native;
    VkRect2D scissor{};
    scissor.offset = { static_cast<int32_t>(x), static_cast<int32_t>(y) };
    scissor.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
    vkCmdSetScissor(data->commandBuffer, 0, 1, &scissor);
}

void RenderPassEncoder::setStencilReference(uint32_t reference) {
    auto data = (RenderPassEncoderHandle *)native;
    vkCmdSetStencilReference(data->commandBuffer,
                             VK_STENCIL_FACE_FRONT_AND_BACK,
                             reference);
}

void RenderPassEncoder::setVertexBuffer(uint32_t slot, std::shared_ptr<Buffer> buffer,
                                         uint64_t offset, int64_t size) {
    auto data      = (RenderPassEncoderHandle *)native;
    auto bufHandle = (BufferHandle *)buffer->native;
    vkCmdBindVertexBuffers(data->commandBuffer, slot, 1, &bufHandle->buffer, &offset);
}

void RenderPassEncoder::setViewport(float x, float y, float width, float height,
                                     float minDepth, float maxDepth) {
    auto data = (RenderPassEncoderHandle *)native;
    VkViewport vp{};
    vp.x        = x;
    vp.y        = y;
    vp.width    = width;
    vp.height   = height;
    vp.minDepth = minDepth;
    vp.maxDepth = maxDepth;
    vkCmdSetViewport(data->commandBuffer, 0, 1, &vp);
}
