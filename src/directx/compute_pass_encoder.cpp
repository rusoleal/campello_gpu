#define NOMINMAX
#include "common.hpp"
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/buffer.hpp>

using namespace systems::leal::campello_gpu;

ComputePassEncoder::ComputePassEncoder(void* pd) : native(pd) {}

ComputePassEncoder::~ComputePassEncoder() {
    if (!native) return;
    delete static_cast<ComputePassEncoderHandle*>(native);
}

void ComputePassEncoder::dispatchWorkgroups(uint64_t workgroupCountX,
                                            uint64_t workgroupCountY,
                                            uint64_t workgroupCountZ) {
    if (!native) return;
    auto* h = static_cast<ComputePassEncoderHandle*>(native);
    h->cmdList->Dispatch(static_cast<UINT>(workgroupCountX),
                          static_cast<UINT>(workgroupCountY),
                          static_cast<UINT>(workgroupCountZ));
}

void ComputePassEncoder::dispatchWorkgroupsIndirect(
    std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset) {
    if (!native || !indirectBuffer || !indirectBuffer->native) return;
    auto* h  = static_cast<ComputePassEncoderHandle*>(native);
    auto* bh = static_cast<BufferHandle*>(indirectBuffer->native);
    h->deviceData->ensureDispatchCommandSignature();
    if (!h->deviceData->dispatchCmdSig) return;
    h->cmdList->ExecuteIndirect(h->deviceData->dispatchCmdSig, 1,
                                 bh->resource, indirectOffset, nullptr, 0);
}

void ComputePassEncoder::end() {
    // The command list stays open until CommandEncoder::finish().
    //
    // Writable storage buffers start each command list in COMMON and are
    // promoted to UNORDERED_ACCESS by the dispatches that write them; a
    // promoted write state lasts until the command list ends, so a copy or a
    // read of the result later in this command list (copyBufferToBuffer() to
    // a readback buffer, say) needs an explicit transition first. Back to
    // COMMON, from which the next use is promoted again. Some drivers ran
    // without it; WARP read zeros.
    if (!native) return;
    auto* h = static_cast<ComputePassEncoderHandle*>(native);
    if (h->writtenBuffers.empty()) return;
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(h->writtenBuffers.size());
    for (ID3D12Resource* resource : h->writtenBuffers) {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = resource;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers.push_back(b);
    }
    h->cmdList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    h->writtenBuffers.clear();
}

void ComputePassEncoder::setBindGroup(uint32_t index,
                                      std::shared_ptr<BindGroup> bindGroup,
                                      const std::vector<uint32_t>& dynamicOffsets,
                                      uint64_t dynamicOffsetsStart,
                                      uint64_t dynamicOffsetsLength) {
    if (!native || !bindGroup || !bindGroup->native) return;
    auto* h   = static_cast<ComputePassEncoderHandle*>(native);
    auto* bgh = static_cast<BindGroupHandle*>(bindGroup->native);

    // Its writable storage buffers, transitioned back in end().
    for (const auto& weak : bgh->writableBuffers) {
        if (auto buffer = weak.lock()) {
            auto* resource = static_cast<BufferHandle*>(buffer->native)->resource;
            if (std::find(h->writtenBuffers.begin(), h->writtenBuffers.end(), resource) == h->writtenBuffers.end())
                h->writtenBuffers.push_back(resource);
        }
    }

    // Bind group `index`'s root parameters (see BindGroupRootParams).
    const auto params = rootParamsFor(h->groupParams, index);
    if (bgh->gpuHandle.ptr != 0 && params[0] >= 0)
        h->cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(params[0]), bgh->gpuHandle);
    if (bgh->samplerHandle.ptr != 0 && params[1] >= 0)
        h->cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(params[1]), bgh->samplerHandle);
}

void ComputePassEncoder::setPipeline(std::shared_ptr<ComputePipeline> pipeline) {
    if (!native || !pipeline || !pipeline->native) return;
    auto* h   = static_cast<ComputePassEncoderHandle*>(native);
    auto* cph = static_cast<ComputePipelineHandle*>(pipeline->native);
    h->groupParams = cph->groupParams;
    h->cmdList->SetPipelineState(cph->pso);
    h->cmdList->SetComputeRootSignature(cph->rootSignature);
}
