#define NOMINMAX
#include "common.hpp"
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/constants/index_format.hpp>
#include <campello_gpu/bind_group.hpp>
#include <algorithm>

using namespace systems::leal::campello_gpu;

RenderPassEncoder::RenderPassEncoder(void* pd) : native(pd) {}

RenderPassEncoder::~RenderPassEncoder() {
    if (!native) return;
    delete static_cast<RenderPassEncoderHandle*>(native);
}

void RenderPassEncoder::beginOcclusionQuery(uint32_t queryIndex) {
    if (!native) return;
    auto* h = static_cast<RenderPassEncoderHandle*>(native);
    if (!h->queryHeap) return;
    h->activeQueryIndex = queryIndex;
    h->cmdList->BeginQuery(h->queryHeap, h->queryType, queryIndex);
}

void RenderPassEncoder::draw(uint32_t vertexCount, uint32_t instanceCount,
                             uint32_t firstVertex, uint32_t firstInstance) {
    if (!native) return;
    auto* h = static_cast<RenderPassEncoderHandle*>(native);
    h->cmdList->IASetPrimitiveTopology(h->topology);
    h->cmdList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void RenderPassEncoder::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                    uint32_t firstVertex, uint32_t baseVertex,
                                    uint32_t firstInstance) {
    if (!native) return;
    auto* h = static_cast<RenderPassEncoderHandle*>(native);
    if (h->hasIBV) h->cmdList->IASetIndexBuffer(&h->ibv);
    h->cmdList->IASetPrimitiveTopology(h->topology);
    h->cmdList->DrawIndexedInstanced(indexCount, instanceCount,
                                      firstVertex, baseVertex, firstInstance);
}

void RenderPassEncoder::drawIndirect(std::shared_ptr<Buffer> indirectBuffer,
                                     uint64_t indirectOffset) {
    if (!native || !indirectBuffer || !indirectBuffer->native) return;
    auto* h  = static_cast<RenderPassEncoderHandle*>(native);
    auto* bh = static_cast<BufferHandle*>(indirectBuffer->native);
    h->deviceData->ensureDrawCommandSignature();
    if (!h->deviceData->drawCmdSig) return;
    h->cmdList->IASetPrimitiveTopology(h->topology);
    h->cmdList->ExecuteIndirect(h->deviceData->drawCmdSig, 1,
                                 bh->resource, indirectOffset, nullptr, 0);
}

void RenderPassEncoder::drawIndexedIndirect(std::shared_ptr<Buffer> indirectBuffer,
                                            uint64_t indirectOffset) {
    if (!native || !indirectBuffer || !indirectBuffer->native) return;
    auto* h  = static_cast<RenderPassEncoderHandle*>(native);
    auto* bh = static_cast<BufferHandle*>(indirectBuffer->native);
    h->deviceData->ensureDrawIndexedCommandSignature();
    if (!h->deviceData->drawIndexedCmdSig) return;
    if (h->hasIBV) h->cmdList->IASetIndexBuffer(&h->ibv);
    h->cmdList->IASetPrimitiveTopology(h->topology);
    h->cmdList->ExecuteIndirect(h->deviceData->drawIndexedCmdSig, 1,
                                 bh->resource, indirectOffset, nullptr, 0);
}

void RenderPassEncoder::end() {
    // The command list stays open until CommandEncoder::finish().

    // Transition every offscreen color attachment written by this pass from
    // RENDER_TARGET to a shader-readable state, so a later pass can safely
    // sample it (e.g. the next blur pass, or the final BackdropFilter/
    // ClipRRect/ShaderMask composite draw). See TextureHandle::currentState's
    // doc comment in common.hpp for why this is necessary at all on D3D12.
    if (!native) return;
    auto* h = static_cast<RenderPassEncoderHandle*>(native);
    if (h->colorAttachmentTextures.empty()) return;

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(h->colorAttachmentTextures.size());
    for (TextureHandle* th : h->colorAttachmentTextures) {
        if (th->currentState == D3D12_RESOURCE_STATE_RENDER_TARGET) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = th->resource;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers.push_back(barrier);
            th->currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
    }
    if (!barriers.empty())
        h->cmdList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

void RenderPassEncoder::endOcclusionQuery() {
    if (!native) return;
    auto* h = static_cast<RenderPassEncoderHandle*>(native);
    if (!h->queryHeap) return;
    h->cmdList->EndQuery(h->queryHeap, h->queryType, h->activeQueryIndex);
}

void RenderPassEncoder::setIndexBuffer(std::shared_ptr<Buffer> buffer,
                                       IndexFormat indexFormat,
                                       uint64_t offset, int64_t size) {
    if (!native || !buffer || !buffer->native) return;
    auto* h  = static_cast<RenderPassEncoderHandle*>(native);
    auto* bh = static_cast<BufferHandle*>(buffer->native);

    DXGI_FORMAT fmt = (indexFormat == IndexFormat::uint16)
        ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    UINT byteSize = (size < 0)
        ? static_cast<UINT>(bh->size - offset)
        : static_cast<UINT>(size);

    h->ibv.BufferLocation = bh->resource->GetGPUVirtualAddress() + offset;
    h->ibv.SizeInBytes    = byteSize;
    h->ibv.Format         = fmt;
    h->hasIBV             = true;
    h->cmdList->IASetIndexBuffer(&h->ibv);
}

void RenderPassEncoder::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
    if (!native || !pipeline || !pipeline->native) return;
    auto* h   = static_cast<RenderPassEncoderHandle*>(native);
    auto* rph = static_cast<RenderPipelineHandle*>(pipeline->native);
    h->topology       = rph->topology;
    h->vertexStrides  = rph->vertexStrides;
    h->cmdList->SetPipelineState(rph->pso);
    h->cmdList->SetGraphicsRootSignature(rph->rootSignature);
    h->hasRootSignature = true;
}

void RenderPassEncoder::setScissorRect(float x, float y, float width, float height) {
    if (!native) return;
    auto* h = static_cast<RenderPassEncoderHandle*>(native);
    D3D12_RECT r;
    r.left   = static_cast<LONG>(x);
    r.top    = static_cast<LONG>(y);
    r.right  = static_cast<LONG>(x + width);
    r.bottom = static_cast<LONG>(y + height);
    h->cmdList->RSSetScissorRects(1, &r);
}

void RenderPassEncoder::setStencilReference(uint32_t reference) {
    if (!native) return;
    auto* h = static_cast<RenderPassEncoderHandle*>(native);
    h->cmdList->OMSetStencilRef(reference);
}

void RenderPassEncoder::setVertexBuffer(uint32_t slot, std::shared_ptr<Buffer> buffer,
                                        uint64_t offset, int64_t size) {
    if (!native || !buffer || !buffer->native) return;
    auto* h  = static_cast<RenderPassEncoderHandle*>(native);
    auto* bh = static_cast<BufferHandle*>(buffer->native);

    UINT byteSize = (size < 0)
        ? static_cast<UINT>(bh->size - offset)
        : static_cast<UINT>(size);

    UINT stride = (slot < h->vertexStrides.size()) ? h->vertexStrides[slot] : 0;

    D3D12_VERTEX_BUFFER_VIEW vbv;
    vbv.BufferLocation = bh->resource->GetGPUVirtualAddress() + offset;
    vbv.SizeInBytes    = byteSize;
    vbv.StrideInBytes  = stride;
    h->cmdList->IASetVertexBuffers(slot, 1, &vbv);
}

void RenderPassEncoder::setViewport(float x, float y, float width, float height,
                                    float minDepth, float maxDepth) {
    if (!native) return;
    auto* h = static_cast<RenderPassEncoderHandle*>(native);
    D3D12_VIEWPORT vp;
    vp.TopLeftX = x;
    vp.TopLeftY = y;
    vp.Width    = width;
    vp.Height   = height;
    vp.MinDepth = minDepth;
    vp.MaxDepth = maxDepth;
    h->cmdList->RSSetViewports(1, &vp);
}

void RenderPassEncoder::setBindGroup(uint32_t index, std::shared_ptr<BindGroup> bindGroup,
                                     const std::vector<uint32_t>& dynamicOffsets,
                                     uint64_t dynamicOffsetsStart,
                                     uint64_t dynamicOffsetsLength) {
    if (!native) return;
    if (!bindGroup) return;
    auto* h   = static_cast<RenderPassEncoderHandle*>(native);
    if (!h->cmdList) return;
    // SetGraphicsRootDescriptorTable() is undefined behavior before any
    // SetGraphicsRootSignature() call on this command list -- see
    // RenderPassEncoderHandle::hasRootSignature's doc comment.
    if (!h->hasRootSignature) return;
    if (!bindGroup->native) return;
    auto* bgh = static_cast<BindGroupHandle*>(bindGroup->native);

    if (bgh->gpuHandle.ptr != 0)
        h->cmdList->SetGraphicsRootDescriptorTable(index, bgh->gpuHandle);
    if (bgh->samplerHandle.ptr != 0)
        h->cmdList->SetGraphicsRootDescriptorTable(index + 1, bgh->samplerHandle);
}
