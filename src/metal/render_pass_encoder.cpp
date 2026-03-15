#include "Metal.hpp"
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/render_pipeline.hpp>

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
    enc->drawPrimitives(
        MTL::PrimitiveTypeTriangle,
        static_cast<MTL::Buffer *>(indirectBuffer->native),
        indirectOffset);
}

void RenderPassEncoder::drawIndexedIndirect(std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset) {
    auto *data = static_cast<MetalRenderEncoderData *>(native);
    if (!data->indexBuffer) return;
    data->encoder->drawIndexedPrimitives(
        MTL::PrimitiveTypeTriangle,
        data->indexType,
        data->indexBuffer,
        data->indexOffset,
        static_cast<MTL::Buffer *>(indirectBuffer->native),
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
    data->indexBuffer = static_cast<MTL::Buffer *>(buffer->native);
    data->indexType   = (indexFormat == IndexFormat::uint32)
                            ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16;
    data->indexOffset = (NS::UInteger)offset;
}

void RenderPassEncoder::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
    auto *enc = static_cast<MetalRenderEncoderData *>(native)->encoder;
    enc->setRenderPipelineState(
        static_cast<MTL::RenderPipelineState *>(pipeline->native));
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
    enc->setVertexBuffer(static_cast<MTL::Buffer *>(buffer->native), offset, slot);
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
