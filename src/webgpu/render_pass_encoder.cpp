#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/bind_group.hpp>
#include "render_pass_encoder_handle.hpp"
#include "buffer_handle.hpp"
#include "render_pipeline_handle.hpp"
#include "bind_group_handle.hpp"

using namespace systems::leal::campello_gpu;

RenderPassEncoder::RenderPassEncoder(void* pd) {
    native = pd;
}

RenderPassEncoder::~RenderPassEncoder() {
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    if (handle->encoder) {
        wgpuRenderPassEncoderRelease(handle->encoder);
    }
    delete handle;
}

void RenderPassEncoder::beginOcclusionQuery(uint32_t queryIndex) {
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    wgpuRenderPassEncoderBeginOcclusionQuery(handle->encoder, queryIndex);
}

void RenderPassEncoder::draw(uint32_t vertexCount, uint32_t instanceCount,
                              uint32_t firstVertex, uint32_t firstInstance) {
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    wgpuRenderPassEncoderDraw(handle->encoder, vertexCount, instanceCount, firstVertex, firstInstance);
    if (handle->deviceData) {
        handle->deviceData->drawCalls++;
    }
}

void RenderPassEncoder::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                     uint32_t firstVertex, uint32_t baseVertex,
                                     uint32_t firstInstance) {
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    wgpuRenderPassEncoderDrawIndexed(handle->encoder, indexCount, instanceCount,
                                     firstVertex, static_cast<int32_t>(baseVertex), firstInstance);
    if (handle->deviceData) {
        handle->deviceData->drawCalls++;
    }
}

void RenderPassEncoder::drawIndirect(std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset) {
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    auto* bufHandle = static_cast<BufferHandle*>(indirectBuffer->native);
    wgpuRenderPassEncoderDrawIndirect(handle->encoder, bufHandle->buffer, indirectOffset);
    if (handle->deviceData) {
        handle->deviceData->drawCalls++;
    }
}

void RenderPassEncoder::drawIndexedIndirect(std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset) {
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    auto* bufHandle = static_cast<BufferHandle*>(indirectBuffer->native);
    wgpuRenderPassEncoderDrawIndexedIndirect(handle->encoder, bufHandle->buffer, indirectOffset);
    if (handle->deviceData) {
        handle->deviceData->drawCalls++;
    }
}

void RenderPassEncoder::end() {
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    wgpuRenderPassEncoderEnd(handle->encoder);
    wgpuRenderPassEncoderRelease(handle->encoder);
    handle->encoder = nullptr;
}

void RenderPassEncoder::endOcclusionQuery() {
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    wgpuRenderPassEncoderEndOcclusionQuery(handle->encoder);
}

void RenderPassEncoder::setIndexBuffer(std::shared_ptr<Buffer> buffer, IndexFormat indexFormat,
                                        uint64_t offset, int64_t size) {
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    auto* bufHandle = static_cast<BufferHandle*>(buffer->native);
    WGPUIndexFormat format = toWGPUIndexFormat(indexFormat);
    uint64_t s = (size < 0) ? bufHandle->size - offset : static_cast<uint64_t>(size);
    wgpuRenderPassEncoderSetIndexBuffer(handle->encoder, bufHandle->buffer, format, offset, s);
}

void RenderPassEncoder::setBindGroup(uint32_t index, std::shared_ptr<BindGroup> bindGroup,
                                      const std::vector<uint32_t>& dynamicOffsets,
                                      uint64_t dynamicOffsetsStart,
                                      uint64_t dynamicOffsetsLength) {
    if (!native || !bindGroup) return;
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    auto* bgHandle = static_cast<BindGroupHandle*>(bindGroup->native);
    const uint32_t* offsets = dynamicOffsets.empty() ? nullptr : dynamicOffsets.data() + dynamicOffsetsStart;
    uint32_t count = static_cast<uint32_t>(dynamicOffsetsLength);
    if (count == 0 && !dynamicOffsets.empty()) {
        count = static_cast<uint32_t>(dynamicOffsets.size() - dynamicOffsetsStart);
    }
    wgpuRenderPassEncoderSetBindGroup(handle->encoder, index, bgHandle->bindGroup, count, offsets);
}

void RenderPassEncoder::setPushConstants(ShaderStage, uint32_t, uint32_t, const void*) {
    // No-op — WebGPU has no push-constant concept (proposal exists but
    // isn't part of the stable API this backend targets). Nothing in this
    // codebase calls setPushConstants() on the WebGPU backend today.
}

void RenderPassEncoder::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    auto* rpHandle = static_cast<RenderPipelineHandle*>(pipeline->native);
    handle->currentPipeline = rpHandle->pipeline;
    wgpuRenderPassEncoderSetPipeline(handle->encoder, rpHandle->pipeline);
}

void RenderPassEncoder::setScissorRect(float x, float y, float width, float height) {
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    wgpuRenderPassEncoderSetScissorRect(handle->encoder,
                                        static_cast<uint32_t>(x),
                                        static_cast<uint32_t>(y),
                                        static_cast<uint32_t>(width),
                                        static_cast<uint32_t>(height));
}

void RenderPassEncoder::setStencilReference(uint32_t reference) {
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    wgpuRenderPassEncoderSetStencilReference(handle->encoder, reference);
}

void RenderPassEncoder::setVertexBuffer(uint32_t slot, std::shared_ptr<Buffer> buffer,
                                         uint64_t offset, int64_t size) {
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    auto* bufHandle = static_cast<BufferHandle*>(buffer->native);
    uint64_t s = (size < 0) ? bufHandle->size - offset : static_cast<uint64_t>(size);
    wgpuRenderPassEncoderSetVertexBuffer(handle->encoder, slot, bufHandle->buffer, offset, s);
}

void RenderPassEncoder::setViewport(float x, float y, float width, float height,
                                     float minDepth, float maxDepth) {
    auto* handle = static_cast<RenderPassEncoderHandle*>(native);
    wgpuRenderPassEncoderSetViewport(handle->encoder, x, y, width, height, minDepth, maxDepth);
}
