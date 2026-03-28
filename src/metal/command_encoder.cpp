#include "Metal.hpp"
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>

using namespace systems::leal::campello_gpu;

CommandEncoder::CommandEncoder(void *pd) : native(pd) {}

CommandEncoder::~CommandEncoder() {
    if (native != nullptr) {
        static_cast<MTL::CommandBuffer *>(native)->release();
    }
}

std::shared_ptr<RenderPassEncoder> CommandEncoder::beginRenderPass(const BeginRenderPassDescriptor &descriptor) {
    auto *cmdBuffer = static_cast<MTL::CommandBuffer *>(native);
    auto *passDesc  = MTL::RenderPassDescriptor::alloc()->init();

    for (size_t i = 0; i < descriptor.colorAttachments.size(); i++) {
        const auto &ca  = descriptor.colorAttachments[i];
        auto       *att = passDesc->colorAttachments()->object(i);
        if (ca.view) {
            att->setTexture(static_cast<MTL::Texture *>(ca.view->native));
        }
        if (ca.resolveTarget) {
            att->setResolveTexture(static_cast<MTL::Texture *>(ca.resolveTarget->native));
            att->setStoreAction(MTL::StoreActionMultisampleResolve);
        } else {
            att->setStoreAction(ca.storeOp == StoreOp::store
                ? MTL::StoreActionStore : MTL::StoreActionDontCare);
        }
        att->setLoadAction(ca.loadOp == LoadOp::clear
            ? MTL::LoadActionClear : MTL::LoadActionLoad);
        att->setClearColor(MTL::ClearColor::Make(
            ca.clearValue[0], ca.clearValue[1], ca.clearValue[2], ca.clearValue[3]));
    }

    if (descriptor.depthStencilAttachment) {
        const auto &dsa = *descriptor.depthStencilAttachment;
        if (dsa.view) {
            auto *mtlTex = static_cast<MTL::Texture *>(dsa.view->native);

            auto *depthAtt = passDesc->depthAttachment();
            depthAtt->setTexture(mtlTex);
            depthAtt->setLoadAction(dsa.depthLoadOp == LoadOp::clear
                ? MTL::LoadActionClear : MTL::LoadActionLoad);
            depthAtt->setStoreAction(dsa.depthStoreOp == StoreOp::store
                ? MTL::StoreActionStore : MTL::StoreActionDontCare);
            depthAtt->setClearDepth(dsa.depthClearValue);

            auto *stencilAtt = passDesc->stencilAttachment();
            stencilAtt->setTexture(mtlTex);
            stencilAtt->setLoadAction(dsa.stencilLoadOp == LoadOp::clear
                ? MTL::LoadActionClear : MTL::LoadActionLoad);
            stencilAtt->setStoreAction(dsa.stencilStoreOp == StoreOp::store
                ? MTL::StoreActionStore : MTL::StoreActionDontCare);
            stencilAtt->setClearStencil(dsa.stencilClearValue);
        }
    }

    auto *encoder = cmdBuffer->renderCommandEncoder(passDesc);
    passDesc->release();
    if (!encoder) return nullptr;
    return std::shared_ptr<RenderPassEncoder>(new RenderPassEncoder(encoder));
}

std::shared_ptr<ComputePassEncoder> CommandEncoder::beginComputePass() {
    auto *cmdBuffer = static_cast<MTL::CommandBuffer *>(native);
    auto *encoder   = cmdBuffer->computeCommandEncoder();
    if (!encoder) return nullptr;
    return std::shared_ptr<ComputePassEncoder>(new ComputePassEncoder(encoder));
}

void CommandEncoder::clearBuffer(std::shared_ptr<Buffer> buffer, uint64_t offset, uint64_t size) {
    auto *cmdBuffer  = static_cast<MTL::CommandBuffer *>(native);
    auto *blitEncoder = cmdBuffer->blitCommandEncoder();
    blitEncoder->fillBuffer(
        static_cast<MTL::Buffer *>(buffer->native),
        NS::Range::Make(offset, size), 0);
    blitEncoder->endEncoding();
}

void CommandEncoder::copyBufferToBuffer(
    std::shared_ptr<Buffer> source, uint64_t sourceOffset,
    std::shared_ptr<Buffer> destination, uint64_t destinationOffset,
    uint64_t size) {

    auto *cmdBuffer   = static_cast<MTL::CommandBuffer *>(native);
    auto *blitEncoder = cmdBuffer->blitCommandEncoder();
    blitEncoder->copyFromBuffer(
        static_cast<MTL::Buffer *>(source->native), sourceOffset,
        static_cast<MTL::Buffer *>(destination->native), destinationOffset,
        size);
    blitEncoder->endEncoding();
}

void CommandEncoder::copyBufferToTexture() {
    // TODO: API needs explicit source buffer / destination texture parameters.
}

void CommandEncoder::copyTextureToBuffer(
    std::shared_ptr<Texture> source,
    uint32_t mipLevel,
    uint32_t arrayLayer,
    std::shared_ptr<Buffer> destination,
    uint64_t destinationOffset,
    uint64_t bytesPerRow)
{
    if (!native || !source || !destination) return;
    auto *cmdBuffer = static_cast<MTL::CommandBuffer *>(native);
    auto *tex       = static_cast<MTL::Texture *>(source->native);
    auto *buf       = static_cast<MTL::Buffer *>(destination->native);

    auto *blitEncoder = cmdBuffer->blitCommandEncoder();
    if (!blitEncoder) return;

    // Calculate the source region (full mip level size)
    uint32_t width  = std::max(1u, (uint32_t)tex->width() >> mipLevel);
    uint32_t height = std::max(1u, (uint32_t)tex->height() >> mipLevel);
    uint32_t depth  = std::max(1u, (uint32_t)tex->depth() >> mipLevel);

    MTL::Origin origin = MTL::Origin::Make(0, 0, 0);
    MTL::Size   size   = MTL::Size::Make(width, height, depth);

    // Copy from texture to buffer
    blitEncoder->copyFromTexture(
        tex,                              // sourceTexture
        arrayLayer,                       // sourceSlice
        mipLevel,                         // sourceLevel
        origin,                           // sourceOrigin
        size,                             // sourceSize
        buf,                              // destinationBuffer
        destinationOffset,                // destinationOffset
        bytesPerRow,                      // destinationBytesPerRow
        bytesPerRow * height              // destinationBytesPerImage (for 3D/array)
    );

    // Synchronize for CPU read access if using managed storage
    blitEncoder->synchronizeResource(buf);

    blitEncoder->endEncoding();
}

void CommandEncoder::copyTextureToTexture() {
    // TODO: API needs explicit source / destination texture parameters.
}

std::shared_ptr<CommandBuffer> CommandEncoder::finish() {
    auto *cmdBuffer = static_cast<MTL::CommandBuffer *>(native);
    cmdBuffer->retain();
    return std::shared_ptr<CommandBuffer>(new CommandBuffer(cmdBuffer));
}

void CommandEncoder::resolveQuerySet(
    std::shared_ptr<QuerySet> querySet,
    uint32_t firstQuery, uint32_t queryCount,
    std::shared_ptr<Buffer> destination, uint64_t destinationOffset) {

    // Occlusion query results are already written to the QuerySet buffer by the GPU.
    // Copy the relevant region to the destination buffer.
    auto *cmdBuffer   = static_cast<MTL::CommandBuffer *>(native);
    auto *blitEncoder = cmdBuffer->blitCommandEncoder();
    blitEncoder->copyFromBuffer(
        static_cast<MTL::Buffer *>(querySet->native),
        firstQuery * sizeof(uint64_t),
        static_cast<MTL::Buffer *>(destination->native),
        destinationOffset,
        queryCount * sizeof(uint64_t));
    blitEncoder->endEncoding();
}

void CommandEncoder::writeTimestamp(std::shared_ptr<QuerySet> querySet, uint32_t queryIndex) {
    // Metal timestamp sampling requires MTL::CounterSampleBuffer configured on
    // the pass descriptor. Not implementable as a standalone command at this level.
}
