#include "Metal.hpp"
#include "render_pipeline_handle.hpp"
#include "bind_group_data.hpp"
#include "buffer_handle.hpp"
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/sampler.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>

using namespace systems::leal::campello_gpu;

// Internal state needed to support setIndexBuffer + drawIndexed.
struct MetalRenderEncoderData {
    MTL::RenderCommandEncoder *encoder;
    MTL::Buffer               *indexBuffer;
    MTL::IndexType             indexType;
    NS::UInteger               indexOffset;

    MetalRenderEncoderData(MTL::RenderCommandEncoder *enc)
        : encoder(enc), indexBuffer(nullptr),
          indexType(MTL::IndexTypeUInt16), indexOffset(0) {}
};

RenderPassEncoder::RenderPassEncoder(void *pd) {
    auto *enc = static_cast<MTL::RenderCommandEncoder *>(pd);
    enc->retain(); // renderCommandEncoder() is autoreleased; retain to take ownership
    native = new MetalRenderEncoderData(enc);
}

RenderPassEncoder::~RenderPassEncoder() {
    if (native != nullptr) {
        auto *data = static_cast<MetalRenderEncoderData *>(native);
        data->encoder->release();
        delete data;
    }
}

void RenderPassEncoder::beginOcclusionQuery(uint32_t queryIndex) {
    auto *enc = static_cast<MetalRenderEncoderData *>(native)->encoder;
    enc->setVisibilityResultMode(
        MTL::VisibilityResultModeCounting, queryIndex * sizeof(uint64_t));
}

void RenderPassEncoder::draw(uint32_t vertexCount, uint32_t instanceCount,
                             uint32_t firstVertex, uint32_t firstInstance) {
    auto *enc = static_cast<MetalRenderEncoderData *>(native)->encoder;
    enc->drawPrimitives(
        MTL::PrimitiveTypeTriangle,
        firstVertex, vertexCount, instanceCount, firstInstance);
}

void RenderPassEncoder::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                    uint32_t firstVertex, uint32_t baseVertex,
                                    uint32_t firstInstance) {
    auto *data = static_cast<MetalRenderEncoderData *>(native);
    if (!data->indexBuffer) return;
    data->encoder->drawIndexedPrimitives(
        MTL::PrimitiveTypeTriangle,
        indexCount,
        data->indexType,
        data->indexBuffer,
        data->indexOffset,
        instanceCount,
        (NS::Integer)baseVertex,
        firstInstance);
}

void RenderPassEncoder::drawIndirect(std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset) {
    auto *enc = static_cast<MetalRenderEncoderData *>(native)->encoder;
    auto *bufHandle = static_cast<MetalBufferHandle *>(indirectBuffer->native);
    enc->drawPrimitives(
        MTL::PrimitiveTypeTriangle,
        bufHandle->buffer,
        indirectOffset);
}

void RenderPassEncoder::drawIndexedIndirect(std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset) {
    auto *data = static_cast<MetalRenderEncoderData *>(native);
    if (!data->indexBuffer) return;
    auto *bufHandle = static_cast<MetalBufferHandle *>(indirectBuffer->native);
    data->encoder->drawIndexedPrimitives(
        MTL::PrimitiveTypeTriangle,
        data->indexType,
        data->indexBuffer,
        data->indexOffset,
        bufHandle->buffer,
        indirectOffset);
}

void RenderPassEncoder::end() {
    static_cast<MetalRenderEncoderData *>(native)->encoder->endEncoding();
}

void RenderPassEncoder::endOcclusionQuery() {
    auto *enc = static_cast<MetalRenderEncoderData *>(native)->encoder;
    enc->setVisibilityResultMode(MTL::VisibilityResultModeDisabled, 0);
}

void RenderPassEncoder::setIndexBuffer(std::shared_ptr<Buffer> buffer,
                                       IndexFormat indexFormat,
                                       uint64_t offset, int64_t size) {
    auto *data        = static_cast<MetalRenderEncoderData *>(native);
    auto *bufHandle   = static_cast<MetalBufferHandle *>(buffer->native);
    data->indexBuffer = bufHandle->buffer;
    data->indexType   = (indexFormat == IndexFormat::uint32)
                            ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16;
    data->indexOffset = (NS::UInteger)offset;
}

void RenderPassEncoder::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
    auto *enc    = static_cast<MetalRenderEncoderData *>(native)->encoder;
    auto *handle = static_cast<MetalRenderPipelineData *>(pipeline->native);
    enc->setRenderPipelineState(handle->pipelineState);
    if (handle->depthStencilState)
        enc->setDepthStencilState(handle->depthStencilState);
}

void RenderPassEncoder::setScissorRect(float x, float y, float width, float height) {
    MTL::ScissorRect rect;
    rect.x      = (NS::UInteger)x;
    rect.y      = (NS::UInteger)y;
    rect.width  = (NS::UInteger)width;
    rect.height = (NS::UInteger)height;
    static_cast<MetalRenderEncoderData *>(native)->encoder->setScissorRect(rect);
}

void RenderPassEncoder::setStencilReference(uint32_t reference) {
    static_cast<MetalRenderEncoderData *>(native)->encoder->setStencilReferenceValue(reference);
}

void RenderPassEncoder::setVertexBuffer(uint32_t slot, std::shared_ptr<Buffer> buffer,
                                        uint64_t offset, int64_t size) {
    auto *enc = static_cast<MetalRenderEncoderData *>(native)->encoder;
    auto *bufHandle = static_cast<MetalBufferHandle *>(buffer->native);
    enc->setVertexBuffer(bufHandle->buffer, offset, slot);
}

void RenderPassEncoder::setViewport(float x, float y, float width, float height,
                                    float minDepth, float maxDepth) {
    MTL::Viewport vp;
    vp.originX = x;
    vp.originY = y;
    vp.width   = width;
    vp.height  = height;
    vp.znear   = minDepth;
    vp.zfar    = maxDepth;
    static_cast<MetalRenderEncoderData *>(native)->encoder->setViewport(vp);
}

void RenderPassEncoder::setBindGroup(uint32_t index, std::shared_ptr<BindGroup> bindGroup,
                                     const std::vector<uint32_t> &dynamicOffsets,
                                     uint64_t dynamicOffsetsStart,
                                     uint64_t dynamicOffsetsLength) {
    if (!native) return;
    if (!bindGroup) return;
    auto *enc    = static_cast<MetalRenderEncoderData *>(native)->encoder;
    if (!enc) return;
    auto *bgData = static_cast<MetalBindGroupData *>(bindGroup->native);
    if (!bgData) return;

    for (const auto &entry : bgData->entries) {
        if (std::holds_alternative<std::shared_ptr<Texture>>(entry.resource)) {
            auto *tex = static_cast<MTL::Texture *>(
                std::get<std::shared_ptr<Texture>>(entry.resource)->native);
            enc->setVertexTexture(tex, entry.binding);
            enc->setFragmentTexture(tex, entry.binding);
        } else if (std::holds_alternative<std::shared_ptr<Sampler>>(entry.resource)) {
            auto *samp = static_cast<MTL::SamplerState *>(
                std::get<std::shared_ptr<Sampler>>(entry.resource)->native);
            enc->setVertexSamplerState(samp, entry.binding);
            enc->setFragmentSamplerState(samp, entry.binding);
        } else if (std::holds_alternative<BufferBinding>(entry.resource)) {
            const auto &bb  = std::get<BufferBinding>(entry.resource);
            auto *bufHandle = static_cast<MetalBufferHandle *>(bb.buffer->native);
            auto       *buf = bufHandle->buffer;
            enc->setVertexBuffer(buf, bb.offset, entry.binding);
            enc->setFragmentBuffer(buf, bb.offset, entry.binding);
        }
    }
}
