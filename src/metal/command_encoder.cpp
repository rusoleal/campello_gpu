#include "Metal.hpp"
#include "acceleration_structure_handle.hpp"
#include "acceleration_structure_helpers.hpp"
#include "buffer_handle.hpp"
#include "texture_handle.hpp"
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <campello_gpu/descriptors/bottom_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/top_level_acceleration_structure_descriptor.hpp>

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

            auto texFormat = mtlTex->pixelFormat();
            bool hasStencil = (texFormat == MTL::PixelFormatDepth32Float_Stencil8)
                             || (texFormat == MTL::PixelFormatDepth24Unorm_Stencil8);

            if (hasStencil) {
                auto *stencilAtt = passDesc->stencilAttachment();
                stencilAtt->setTexture(mtlTex);
                stencilAtt->setLoadAction(dsa.stencilLoadOp == LoadOp::clear
                    ? MTL::LoadActionClear : MTL::LoadActionLoad);
                stencilAtt->setStoreAction(dsa.stencilStoreOp == StoreOp::store
                    ? MTL::StoreActionStore : MTL::StoreActionDontCare);
                stencilAtt->setClearStencil(dsa.stencilClearValue);
            }
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
    auto *bufferHandle = static_cast<MetalBufferHandle *>(buffer->native);
    blitEncoder->fillBuffer(
        bufferHandle->buffer,
        NS::Range::Make(offset, size), 0);
    blitEncoder->endEncoding();
}

void CommandEncoder::copyBufferToBuffer(
    std::shared_ptr<Buffer> source, uint64_t sourceOffset,
    std::shared_ptr<Buffer> destination, uint64_t destinationOffset,
    uint64_t size) {

    auto *cmdBuffer   = static_cast<MTL::CommandBuffer *>(native);
    auto *blitEncoder = cmdBuffer->blitCommandEncoder();
    auto *sourceHandle = static_cast<MetalBufferHandle *>(source->native);
    auto *destHandle = static_cast<MetalBufferHandle *>(destination->native);
    blitEncoder->copyFromBuffer(
        sourceHandle->buffer, sourceOffset,
        destHandle->buffer, destinationOffset,
        size);
    blitEncoder->endEncoding();
}

void CommandEncoder::copyBufferToTexture(
    std::shared_ptr<Buffer>  source,
    uint64_t                 sourceOffset,
    uint64_t                 bytesPerRow,
    std::shared_ptr<Texture> destination,
    uint32_t                 mipLevel,
    uint32_t                 arrayLayer)
{
    if (!native || !source || !destination) return;
    auto *cmdBuffer  = static_cast<MTL::CommandBuffer *>(native);
    auto *bufHandle  = static_cast<MetalBufferHandle *>(source->native);
    auto *texHandle  = static_cast<MetalTextureHandle *>(destination->native);
    auto *blitEncoder = cmdBuffer->blitCommandEncoder();
    if (!blitEncoder) return;

    auto *buf = bufHandle->buffer;
    auto *tex = texHandle->texture;

    uint32_t mipWidth  = std::max(1u, (uint32_t)tex->width()  >> mipLevel);
    uint32_t mipHeight = std::max(1u, (uint32_t)tex->height() >> mipLevel);
    uint32_t mipDepth  = std::max(1u, (uint32_t)tex->depth()  >> mipLevel);

    NS::UInteger rowBytes = (bytesPerRow > 0) ? static_cast<NS::UInteger>(bytesPerRow)
                                               : mipWidth * (tex->pixelFormat() == MTL::PixelFormatRGBA8Unorm ? 4 : 4);

    blitEncoder->copyFromBuffer(
        buf,
        static_cast<NS::UInteger>(sourceOffset),
        rowBytes,
        rowBytes * mipHeight,
        MTL::Size::Make(mipWidth, mipHeight, mipDepth),
        tex,
        arrayLayer,
        mipLevel,
        MTL::Origin::Make(0, 0, 0));
    blitEncoder->endEncoding();
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
    auto *texHandle = static_cast<MetalTextureHandle *>(source->native);
    auto *bufHandle = static_cast<MetalBufferHandle *>(destination->native);
    auto *tex       = texHandle->texture;
    auto *buf       = bufHandle->buffer;

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

void CommandEncoder::copyTextureToTexture(
    std::shared_ptr<Texture> source,
    uint32_t srcMipLevel,
    const Offset3D& sourceOffset,
    std::shared_ptr<Texture> destination,
    uint32_t dstMipLevel,
    const Offset3D& destinationOffset,
    const Extent3D& extent)
{
    if (!native || !source || !destination) return;

    auto *cmdBuffer  = static_cast<MTL::CommandBuffer *>(native);
    auto *srcHandle  = static_cast<MetalTextureHandle *>(source->native);
    auto *dstHandle  = static_cast<MetalTextureHandle *>(destination->native);
    auto *srcTex     = srcHandle->texture;
    auto *dstTex     = dstHandle->texture;

    auto *blit = cmdBuffer->blitCommandEncoder();
    if (!blit) return;

    blit->copyFromTexture(
        srcTex, 0, srcMipLevel,
        MTL::Origin::Make(sourceOffset.x, sourceOffset.y, sourceOffset.z),
        MTL::Size::Make(extent.width, extent.height, extent.depth),
        dstTex, 0, dstMipLevel,
        MTL::Origin::Make(destinationOffset.x, destinationOffset.y, destinationOffset.z));

    blit->endEncoding();
}

void CommandEncoder::generateMipmaps(std::shared_ptr<Texture> texture) {
    if (!native || !texture || !texture->native) return;
    auto *cmdBuffer  = static_cast<MTL::CommandBuffer *>(native);
    auto *texHandle  = static_cast<MetalTextureHandle *>(texture->native);
    auto *blit       = cmdBuffer->blitCommandEncoder();
    if (!blit) return;
    blit->generateMipmaps(texHandle->texture);
    blit->endEncoding();
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
    auto *destHandle  = static_cast<MetalBufferHandle *>(destination->native);
    blitEncoder->copyFromBuffer(
        static_cast<MTL::Buffer *>(querySet->native),
        firstQuery * sizeof(uint64_t),
        destHandle->buffer,
        destinationOffset,
        queryCount * sizeof(uint64_t));
    blitEncoder->endEncoding();
}

void CommandEncoder::writeTimestamp(std::shared_ptr<QuerySet> querySet, uint32_t queryIndex) {
    // Metal timestamp sampling requires MTL::CounterSampleBuffer configured on
    // the pass descriptor. Not implementable as a standalone command at this level.
}

// ---------------------------------------------------------------------------
// Ray tracing commands
// ---------------------------------------------------------------------------

std::shared_ptr<RayTracingPassEncoder> CommandEncoder::beginRayTracingPass() {
    auto *cmdBuffer = static_cast<MTL::CommandBuffer *>(native);
    auto *encoder   = cmdBuffer->computeCommandEncoder();
    if (!encoder) return nullptr;
    return std::shared_ptr<RayTracingPassEncoder>(new RayTracingPassEncoder(encoder));
}

void CommandEncoder::buildAccelerationStructure(
    std::shared_ptr<AccelerationStructure> dst,
    const BottomLevelAccelerationStructureDescriptor &descriptor,
    std::shared_ptr<Buffer> scratchBuffer)
{
    if (!dst || !scratchBuffer) return;
    auto *cmdBuffer = static_cast<MTL::CommandBuffer *>(native);
    auto *asEncoder = cmdBuffer->accelerationStructureCommandEncoder();
    if (!asEncoder) return;

    auto bufToMTL = [](const std::shared_ptr<Buffer> &b) -> MTL::Buffer * {
        if (!b) return nullptr;
        auto *handle = static_cast<MetalBufferHandle *>(b->native);
        return handle ? handle->buffer : nullptr;
    };
    auto *primDesc = makePrimASDescriptor(descriptor, bufToMTL);
    auto *scratchHandle = static_cast<MetalBufferHandle *>(scratchBuffer->native);
    asEncoder->buildAccelerationStructure(
        static_cast<MetalAccelerationStructureData *>(dst->native)->accelerationStructure,
        primDesc,
        scratchHandle->buffer,
        0);
    primDesc->release();
    asEncoder->endEncoding();
}

void CommandEncoder::buildAccelerationStructure(
    std::shared_ptr<AccelerationStructure> dst,
    const TopLevelAccelerationStructureDescriptor &descriptor,
    std::shared_ptr<Buffer> scratchBuffer)
{
    if (!dst || !scratchBuffer) return;
    auto *cmdBuffer = static_cast<MTL::CommandBuffer *>(native);
    auto *asEncoder = cmdBuffer->accelerationStructureCommandEncoder();
    if (!asEncoder) return;

    // We need the device to build the instance buffer. Obtain it from the AS object.
    // Metal-cpp: accelerationStructure->device() returns the owning device.
    auto *mtlAs = static_cast<MetalAccelerationStructureData *>(dst->native)
                      ->accelerationStructure;
    auto *dev = mtlAs->device();

    auto asToMTLData = [](const std::shared_ptr<AccelerationStructure> &a) {
        return static_cast<MetalAccelerationStructureData *>(a->native);
    };
    auto [instanceDesc, instanceBuf] = makeInstanceASDescriptor(dev, descriptor, asToMTLData);
    if (!instanceDesc) { asEncoder->endEncoding(); return; }

    auto *scratchHandle = static_cast<MetalBufferHandle *>(scratchBuffer->native);
    asEncoder->buildAccelerationStructure(
        mtlAs,
        instanceDesc,
        scratchHandle->buffer,
        0);
    instanceDesc->release();
    instanceBuf->release();
    asEncoder->endEncoding();
}

void CommandEncoder::updateAccelerationStructure(
    std::shared_ptr<AccelerationStructure> src,
    std::shared_ptr<AccelerationStructure> dst,
    std::shared_ptr<Buffer> scratchBuffer)
{
    if (!src || !dst || !scratchBuffer) return;
    auto *cmdBuffer = static_cast<MTL::CommandBuffer *>(native);
    auto *asEncoder = cmdBuffer->accelerationStructureCommandEncoder();
    if (!asEncoder) return;

    auto *scratchHandle = static_cast<MetalBufferHandle *>(scratchBuffer->native);
    // Pass nullptr as the descriptor to refit using the original build geometry.
    asEncoder->refitAccelerationStructure(
        static_cast<MetalAccelerationStructureData *>(src->native)->accelerationStructure,
        nullptr,
        static_cast<MetalAccelerationStructureData *>(dst->native)->accelerationStructure,
        scratchHandle->buffer,
        0);
    asEncoder->endEncoding();
}

void CommandEncoder::copyAccelerationStructure(
    std::shared_ptr<AccelerationStructure> src,
    std::shared_ptr<AccelerationStructure> dst)
{
    if (!src || !dst) return;
    auto *cmdBuffer = static_cast<MTL::CommandBuffer *>(native);
    auto *asEncoder = cmdBuffer->accelerationStructureCommandEncoder();
    if (!asEncoder) return;

    asEncoder->copyAccelerationStructure(
        static_cast<MetalAccelerationStructureData *>(src->native)->accelerationStructure,
        static_cast<MetalAccelerationStructureData *>(dst->native)->accelerationStructure);
    asEncoder->endEncoding();
}
