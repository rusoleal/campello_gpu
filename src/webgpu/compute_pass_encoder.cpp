#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/bind_group.hpp>
#include "compute_pass_encoder_handle.hpp"
#include "compute_pipeline_handle.hpp"
#include "bind_group_handle.hpp"
#include "buffer_handle.hpp"

using namespace systems::leal::campello_gpu;

ComputePassEncoder::ComputePassEncoder(void* pd) {
    native = pd;
}

ComputePassEncoder::~ComputePassEncoder() {
    auto* handle = static_cast<ComputePassEncoderHandle*>(native);
    if (handle->encoder) {
        wgpuComputePassEncoderRelease(handle->encoder);
    }
    delete handle;
}

void ComputePassEncoder::dispatchWorkgroups(uint64_t workgroupCountX,
                                             uint64_t workgroupCountY,
                                             uint64_t workgroupCountZ) {
    auto* handle = static_cast<ComputePassEncoderHandle*>(native);
    wgpuComputePassEncoderDispatchWorkgroups(handle->encoder,
                                             static_cast<uint32_t>(workgroupCountX),
                                             static_cast<uint32_t>(workgroupCountY),
                                             static_cast<uint32_t>(workgroupCountZ));
    if (handle->deviceData) {
        handle->deviceData->dispatchCalls++;
    }
}

void ComputePassEncoder::dispatchWorkgroupsIndirect(std::shared_ptr<Buffer> indirectBuffer,
                                                     uint64_t indirectOffset) {
    auto* handle = static_cast<ComputePassEncoderHandle*>(native);
    auto* bufHandle = static_cast<BufferHandle*>(indirectBuffer->native);
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(handle->encoder, bufHandle->buffer, indirectOffset);
    if (handle->deviceData) {
        handle->deviceData->dispatchCalls++;
    }
}

void ComputePassEncoder::end() {
    auto* handle = static_cast<ComputePassEncoderHandle*>(native);
    wgpuComputePassEncoderEnd(handle->encoder);
    wgpuComputePassEncoderRelease(handle->encoder);
    handle->encoder = nullptr;
}

void ComputePassEncoder::setBindGroup(uint32_t index, std::shared_ptr<BindGroup> bindGroup,
                                       const std::vector<uint32_t>& dynamicOffsets,
                                       uint64_t dynamicOffsetsStart,
                                       uint64_t dynamicOffsetsLength) {
    if (!native || !bindGroup) return;
    auto* handle = static_cast<ComputePassEncoderHandle*>(native);
    auto* bgHandle = static_cast<BindGroupHandle*>(bindGroup->native);
    const uint32_t* offsets = dynamicOffsets.empty() ? nullptr : dynamicOffsets.data() + dynamicOffsetsStart;
    uint32_t count = static_cast<uint32_t>(dynamicOffsetsLength);
    if (count == 0 && !dynamicOffsets.empty()) {
        count = static_cast<uint32_t>(dynamicOffsets.size() - dynamicOffsetsStart);
    }
    wgpuComputePassEncoderSetBindGroup(handle->encoder, index, bgHandle->bindGroup, count, offsets);
}

void ComputePassEncoder::setPipeline(std::shared_ptr<ComputePipeline> pipeline) {
    auto* handle = static_cast<ComputePassEncoderHandle*>(native);
    auto* cpHandle = static_cast<ComputePipelineHandle*>(pipeline->native);
    handle->currentPipeline = cpHandle->pipeline;
    wgpuComputePassEncoderSetPipeline(handle->encoder, cpHandle->pipeline);
}
