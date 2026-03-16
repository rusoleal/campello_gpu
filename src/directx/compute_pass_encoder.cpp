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
    // ExecuteIndirect requires a command signature; not implemented.
}

void ComputePassEncoder::end() {
    // The command list stays open until CommandEncoder::finish().
}

void ComputePassEncoder::setBindGroup(uint32_t index,
                                      std::shared_ptr<BindGroup> bindGroup,
                                      const std::vector<uint32_t>& dynamicOffsets,
                                      uint64_t dynamicOffsetsStart,
                                      uint64_t dynamicOffsetsLength) {
    if (!native || !bindGroup || !bindGroup->native) return;
    auto* h   = static_cast<ComputePassEncoderHandle*>(native);
    auto* bgh = static_cast<BindGroupHandle*>(bindGroup->native);

    if (bgh->gpuHandle.ptr != 0)
        h->cmdList->SetComputeRootDescriptorTable(index, bgh->gpuHandle);
    if (bgh->samplerHandle.ptr != 0)
        h->cmdList->SetComputeRootDescriptorTable(index + 1, bgh->samplerHandle);
}

void ComputePassEncoder::setPipeline(std::shared_ptr<ComputePipeline> pipeline) {
    if (!native || !pipeline || !pipeline->native) return;
    auto* h   = static_cast<ComputePassEncoderHandle*>(native);
    auto* cph = static_cast<ComputePipelineHandle*>(pipeline->native);
    h->cmdList->SetPipelineState(cph->pso);
    h->cmdList->SetComputeRootSignature(cph->rootSignature);
}
