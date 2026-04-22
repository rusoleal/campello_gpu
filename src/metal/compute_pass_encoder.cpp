#include "Metal.hpp"
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/bind_group.hpp>
#include "bind_group_data.hpp"
#include "buffer_handle.hpp"

using namespace systems::leal::campello_gpu;

// Internal state: stores the encoder and the current pipeline to derive threadgroup size.
struct MetalComputeEncoderData {
    MTL::ComputeCommandEncoder *encoder;
    MTL::ComputePipelineState  *currentPipeline;

    MetalComputeEncoderData(MTL::ComputeCommandEncoder *enc)
        : encoder(enc), currentPipeline(nullptr) {}
};

ComputePassEncoder::ComputePassEncoder(void *pd) {
    auto *enc = static_cast<MTL::ComputeCommandEncoder *>(pd);
    enc->retain(); // computeCommandEncoder() is autoreleased; retain to take ownership
    native = new MetalComputeEncoderData(enc);
}

ComputePassEncoder::~ComputePassEncoder() {
    if (native != nullptr) {
        auto *data = static_cast<MetalComputeEncoderData *>(native);
        data->encoder->release();
        delete data;
    }
}

void ComputePassEncoder::dispatchWorkgroups(uint64_t workgroupCountX,
                                            uint64_t workgroupCountY,
                                            uint64_t workgroupCountZ) {
    auto *data = static_cast<MetalComputeEncoderData *>(native);
    if (!data->currentPipeline) return;
    NS::UInteger threadGroupSize = data->currentPipeline->threadExecutionWidth();
    data->encoder->dispatchThreadgroups(
        MTL::Size::Make(workgroupCountX, workgroupCountY, workgroupCountZ),
        MTL::Size::Make(threadGroupSize, 1, 1));
}

void ComputePassEncoder::dispatchWorkgroupsIndirect(std::shared_ptr<Buffer> indirectBuffer,
                                                    uint64_t indirectOffset) {
    auto *data = static_cast<MetalComputeEncoderData *>(native);
    if (!data->currentPipeline) return;
    NS::UInteger threadGroupSize = data->currentPipeline->threadExecutionWidth();
    data->encoder->dispatchThreadgroups(
        static_cast<MTL::Buffer *>(indirectBuffer->native),
        indirectOffset,
        MTL::Size::Make(threadGroupSize, 1, 1));
}

void ComputePassEncoder::end() {
    static_cast<MetalComputeEncoderData *>(native)->encoder->endEncoding();
}

void ComputePassEncoder::setBindGroup(uint32_t index, std::shared_ptr<BindGroup> bindGroup,
                                      const std::vector<uint32_t> &dynamicOffsets,
                                      uint64_t dynamicOffsetsStart,
                                      uint64_t dynamicOffsetsLength) {
    (void)index; (void)dynamicOffsets; (void)dynamicOffsetsStart; (void)dynamicOffsetsLength;
    if (!bindGroup || !bindGroup->native) return;

    auto *data = static_cast<MetalComputeEncoderData *>(native);
    auto *bgData = static_cast<MetalBindGroupData *>(bindGroup->native);

    for (const auto& entry : bgData->entries) {
        if (const auto* bb = std::get_if<BufferBinding>(&entry.resource)) {
            if (!bb->buffer) continue;
            auto *handle = static_cast<MetalBufferHandle *>(bb->buffer->native);
            data->encoder->setBuffer(handle->buffer,
                                     static_cast<NS::UInteger>(bb->offset),
                                     static_cast<NS::UInteger>(entry.binding));
        }
    }
}

void ComputePassEncoder::setPipeline(std::shared_ptr<ComputePipeline> pipeline) {
    auto *data = static_cast<MetalComputeEncoderData *>(native);
    data->currentPipeline = static_cast<MTL::ComputePipelineState *>(pipeline->native);
    data->encoder->setComputePipelineState(data->currentPipeline);
}
