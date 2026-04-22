#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include "render_pass_encoder_handle.hpp"
#include "buffer_handle.hpp"
#include "render_pipeline_handle.hpp"
#include "bind_group_handle.hpp"
#include "query_set_handle.hpp"

using namespace systems::leal::campello_gpu;

// Extern function pointer for VK_KHR_dynamic_rendering (loaded in device.cpp)
extern PFN_vkCmdEndRenderingKHR pfnCmdEndRenderingKHR;

RenderPassEncoder::RenderPassEncoder(void *pd) {
    native = pd;
    
}

RenderPassEncoder::~RenderPassEncoder() {
    auto data = (RenderPassEncoderHandle *)native;
    delete data;
}

void RenderPassEncoder::beginOcclusionQuery(uint32_t queryIndex) {
    auto data = (RenderPassEncoderHandle *)native;
    if (data->queryPool == VK_NULL_HANDLE) return;
    vkCmdBeginQuery(data->commandBuffer, data->queryPool, queryIndex, 0);
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

    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    if (data->isSwapchain) {
        // Swapchain: transition to PRESENT_SRC_KHR for presentation.
        barrier.image         = data->currentSwapchainImage;
        barrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.dstAccessMask = VK_ACCESS_NONE;
        vkCmdPipelineBarrier(data->commandBuffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    } else {
        // Offscreen: transition to GENERAL so the image is readable for sampling/copy.
        barrier.image         = data->offscreenImage;
        barrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(data->commandBuffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
}

void RenderPassEncoder::endOcclusionQuery() {
    auto data = (RenderPassEncoderHandle *)native;
    if (data->queryPool == VK_NULL_HANDLE) return;
    vkCmdEndQuery(data->commandBuffer, data->queryPool, 0);
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

void RenderPassEncoder::setBindGroup(uint32_t index, std::shared_ptr<BindGroup> bindGroup,
                                      const std::vector<uint32_t> &dynamicOffsets,
                                      uint64_t dynamicOffsetsStart,
                                      uint64_t dynamicOffsetsLength) {
    if (!native) return;
    if (!bindGroup) return;
    auto data = (RenderPassEncoderHandle *)native;
    if (data->commandBuffer == VK_NULL_HANDLE) return;
    if (data->pipelineLayout == VK_NULL_HANDLE) return;
    if (!bindGroup->native) return;
    auto bg   = (BindGroupHandle *)bindGroup->native;
    if (bg->descriptorSet == VK_NULL_HANDLE) return;

    const uint32_t *offsets     = dynamicOffsets.empty() ? nullptr
                                  : dynamicOffsets.data() + dynamicOffsetsStart;
    uint32_t        offsetCount = static_cast<uint32_t>(dynamicOffsetsLength);

    vkCmdBindDescriptorSets(data->commandBuffer,
                             VK_PIPELINE_BIND_POINT_GRAPHICS,
                             data->pipelineLayout,
                             index,
                             1, &bg->descriptorSet,
                             offsetCount, offsets);
}

void RenderPassEncoder::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
    if (!native) return;
    if (!pipeline) return;
    auto data = (RenderPassEncoderHandle *)native;
    if (data->commandBuffer == VK_NULL_HANDLE) return;
    if (!pipeline->native) return;
    auto pipe = (RenderPipelineHandle *)pipeline->native;
    if (pipe->pipeline == VK_NULL_HANDLE) return;
    vkCmdBindPipeline(data->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->pipeline);
    data->pipelineLayout = pipe->pipelineLayout; // cache for setBindGroup
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
