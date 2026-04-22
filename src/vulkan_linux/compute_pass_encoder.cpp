#include <vector>
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include "compute_pass_encoder_handle.hpp"
#include "compute_pipeline_handle.hpp"
#include "bind_group_handle.hpp"
#include "buffer_handle.hpp"

using namespace systems::leal::campello_gpu;

ComputePassEncoder::ComputePassEncoder(void *pd) {
    this->native = pd;
}

ComputePassEncoder::~ComputePassEncoder() {
    auto data = (ComputePassEncoderHandle *)native;
    delete data;
}

void ComputePassEncoder::setPipeline(std::shared_ptr<ComputePipeline> pipeline) {
    if (!native) return;
    if (!pipeline) return;
    auto data = (ComputePassEncoderHandle *)native;
    if (data->commandBuffer == VK_NULL_HANDLE) return;
    if (!pipeline->native) return;
    auto pipe = (ComputePipelineHandle *)pipeline->native;
    if (pipe->pipeline == VK_NULL_HANDLE) return;
    vkCmdBindPipeline(data->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->pipeline);
    data->pipelineLayout = pipe->pipelineLayout; // cache for setBindGroup
}

void ComputePassEncoder::dispatchWorkgroups(uint64_t workgroupCountX,
                                             uint64_t workgroupCountY,
                                             uint64_t workgroupCountZ) {
    auto data = (ComputePassEncoderHandle *)native;
    vkCmdDispatch(data->commandBuffer,
                  static_cast<uint32_t>(workgroupCountX),
                  static_cast<uint32_t>(workgroupCountY),
                  static_cast<uint32_t>(workgroupCountZ));
}

void ComputePassEncoder::dispatchWorkgroupsIndirect(std::shared_ptr<Buffer> indirectBuffer,
                                                     uint64_t indirectOffset) {
    auto data   = (ComputePassEncoderHandle *)native;
    auto bufHandle = (BufferHandle *)indirectBuffer->native;
    vkCmdDispatchIndirect(data->commandBuffer, bufHandle->buffer, indirectOffset);
}

void ComputePassEncoder::setBindGroup(uint32_t index,
                                       std::shared_ptr<BindGroup> bindGroup,
                                       const std::vector<uint32_t> &dynamicOffsets,
                                       uint64_t dynamicOffsetsStart,
                                       uint64_t dynamicOffsetsLength) {
    if (!native) return;
    if (!bindGroup) return;
    auto data = (ComputePassEncoderHandle *)native;
    if (data->commandBuffer == VK_NULL_HANDLE) return;
    if (data->pipelineLayout == VK_NULL_HANDLE) return;
    if (!bindGroup->native) return;
    auto bg   = (BindGroupHandle *)bindGroup->native;
    if (bg->descriptorSet == VK_NULL_HANDLE) return;

    const uint32_t *offsets    = dynamicOffsets.empty() ? nullptr
                                  : dynamicOffsets.data() + dynamicOffsetsStart;
    uint32_t        offsetCount = static_cast<uint32_t>(dynamicOffsetsLength);

    // Pipeline layout not tracked here; pass VK_NULL_HANDLE (requires layout in handle for full impl).
    vkCmdBindDescriptorSets(data->commandBuffer,
                             VK_PIPELINE_BIND_POINT_COMPUTE,
                             data->pipelineLayout,
                             index,
                             1, &bg->descriptorSet,
                             offsetCount, offsets);
}

void ComputePassEncoder::end() {
    // Dynamic rendering does not require an explicit compute pass end command.
}
