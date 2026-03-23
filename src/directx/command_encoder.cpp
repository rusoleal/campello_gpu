#define NOMINMAX
#include "common.hpp"
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>

using namespace systems::leal::campello_gpu;

CommandEncoder::CommandEncoder(void* pd) : native(pd) {}

CommandEncoder::~CommandEncoder() {
    if (!native) return;
    auto* h = static_cast<CommandEncoderHandle*>(native);
    // If finish() was not called the list is still open — close it.
    if (h->cmdList)   { h->cmdList->Close(); h->cmdList->Release(); }
    if (h->allocator) h->allocator->Release();
    delete h;
}

std::shared_ptr<ComputePassEncoder> CommandEncoder::beginComputePass() {
    if (!native) return nullptr;
    auto* h  = static_cast<CommandEncoderHandle*>(native);

    auto* ph          = new ComputePassEncoderHandle();
    ph->cmdList       = h->cmdList;
    ph->deviceData    = h->deviceData;
    return std::shared_ptr<ComputePassEncoder>(new ComputePassEncoder(ph));
}

std::shared_ptr<RenderPassEncoder> CommandEncoder::beginRenderPass(
    const BeginRenderPassDescriptor& descriptor)
{
    if (!native) return nullptr;
    auto* h    = static_cast<CommandEncoderHandle*>(native);
    auto* list = h->cmdList;

    // Set up render target transition + OMSetRenderTargets
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    bool hasDSV = false;

    for (const auto& ca : descriptor.colorAttachments) {
        if (!ca.view || !ca.view->native) continue;
        auto* tvh = static_cast<TextureViewHandle*>(ca.view->native);
        rtvHandles.push_back(tvh->cpuHandle);

        if (ca.loadOp == LoadOp::clear) {
            list->ClearRenderTargetView(tvh->cpuHandle, ca.clearValue, 0, nullptr);
        }
    }

    if (descriptor.depthStencilAttachment) {
        const auto& dsa = *descriptor.depthStencilAttachment;
        if (dsa.view && dsa.view->native) {
            auto* tvh = static_cast<TextureViewHandle*>(dsa.view->native);
            dsvHandle = tvh->cpuHandle;
            hasDSV    = true;

            D3D12_CLEAR_FLAGS flags = {};
            if (dsa.depthLoadOp == LoadOp::clear)   flags |= D3D12_CLEAR_FLAG_DEPTH;
            if (dsa.stencilLoadOp == LoadOp::clear) flags |= D3D12_CLEAR_FLAG_STENCIL;
            if (flags)
                list->ClearDepthStencilView(dsvHandle, flags,
                    dsa.depthClearValue,
                    static_cast<UINT8>(dsa.stencilClearValue), 0, nullptr);
        }
    }

    list->OMSetRenderTargets(
        static_cast<UINT>(rtvHandles.size()),
        rtvHandles.empty() ? nullptr : rtvHandles.data(),
        FALSE,
        hasDSV ? &dsvHandle : nullptr);

    auto* ph        = new RenderPassEncoderHandle();
    ph->cmdList     = list;
    ph->deviceData  = h->deviceData;
    if (descriptor.occlusionQuerySet && descriptor.occlusionQuerySet->native) {
        auto* qsh      = static_cast<QuerySetHandle*>(descriptor.occlusionQuerySet->native);
        ph->queryHeap  = qsh->queryHeap;
        ph->queryType  = qsh->queryType;
    }
    return std::shared_ptr<RenderPassEncoder>(new RenderPassEncoder(ph));
}

void CommandEncoder::clearBuffer(std::shared_ptr<Buffer> buffer,
                                 uint64_t offset, uint64_t size) {
    if (!native || !buffer || !buffer->native) return;
    auto* h   = static_cast<CommandEncoderHandle*>(native);
    auto* bh  = static_cast<BufferHandle*>(buffer->native);
    // Zero-fill via memset on the persistently-mapped upload buffer.
    if (bh->mapped)
        std::memset(static_cast<uint8_t*>(bh->mapped) + offset, 0,
                    std::min(size, bh->size - offset));
}

void CommandEncoder::copyBufferToBuffer(
    std::shared_ptr<Buffer> source,    uint64_t sourceOffset,
    std::shared_ptr<Buffer> destination, uint64_t destinationOffset,
    uint64_t size)
{
    if (!native || !source || !destination) return;
    auto* h    = static_cast<CommandEncoderHandle*>(native);
    auto* src  = static_cast<BufferHandle*>(source->native);
    auto* dst  = static_cast<BufferHandle*>(destination->native);
    h->cmdList->CopyBufferRegion(dst->resource, destinationOffset,
                                  src->resource, sourceOffset, size);
}

void CommandEncoder::copyBufferToTexture()  {}
void CommandEncoder::copyTextureToBuffer()  {}
void CommandEncoder::copyTextureToTexture() {}

std::shared_ptr<CommandBuffer> CommandEncoder::finish() {
    if (!native) return nullptr;
    auto* h = static_cast<CommandEncoderHandle*>(native);

    // Transition swapchain back buffer from RENDER_TARGET back to PRESENT
    if (h->deviceData->swapChain) {
        UINT frameIdx = h->deviceData->frameIndex;
        ID3D12Resource* rt = h->deviceData->renderTargets[frameIdx];
        if (rt) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = rt;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            h->cmdList->ResourceBarrier(1, &barrier);
        }
    }

    if (FAILED(h->cmdList->Close())) return nullptr;

    auto* cbh      = new CommandBufferHandle();
    cbh->cmdList   = h->cmdList;
    cbh->allocator = h->allocator;
    cbh->deviceData = h->deviceData;

    // Ownership transferred to CommandBufferHandle
    h->cmdList   = nullptr;
    h->allocator = nullptr;

    return std::shared_ptr<CommandBuffer>(new CommandBuffer(cbh));
}

void CommandEncoder::resolveQuerySet(
    std::shared_ptr<QuerySet> querySet,
    uint32_t firstQuery, uint32_t queryCount,
    std::shared_ptr<Buffer> destination, uint64_t destinationOffset)
{
    if (!native || !querySet) return;
    auto* h   = static_cast<CommandEncoderHandle*>(native);
    auto* qsh = static_cast<QuerySetHandle*>(querySet->native);
    // Resolve into the QuerySet's own readback buffer (D3D12_HEAP_TYPE_READBACK,
    // already in COPY_DEST state). Upload-heap buffers cannot serve as COPY_DEST.
    if (qsh->resultBuffer)
        h->cmdList->ResolveQueryData(qsh->queryHeap, qsh->queryType,
                                      firstQuery, queryCount,
                                      qsh->resultBuffer, destinationOffset);
}

void CommandEncoder::writeTimestamp(
    std::shared_ptr<QuerySet> querySet, uint32_t queryIndex)
{
    if (!native || !querySet) return;
    auto* h   = static_cast<CommandEncoderHandle*>(native);
    auto* qsh = static_cast<QuerySetHandle*>(querySet->native);
    h->cmdList->EndQuery(qsh->queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
}
