#include "Metal.hpp"
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/bind_group.hpp>

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
    NS::UInteger threadGroupSize = data->currentPipeline
        ? data->currentPipeline->threadExecutionWidth() : 32;
    data->encoder->dispatchThreadgroups(
        MTL::Size::Make(workgroupCountX, workgroupCountY, workgroupCountZ),
        MTL::Size::Make(threadGroupSize, 1, 1));
}

void ComputePassEncoder::dispatchWorkgroupsIndirect(std::shared_ptr<Buffer> indirectBuffer,
                                                    uint64_t indirectOffset) {
    auto *data = static_cast<MetalComputeEncoderData *>(native);
    NS::UInteger threadGroupSize = data->currentPipeline
        ? data->currentPipeline->threadExecutionWidth() : 32;
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
    // Metal uses direct buffer/texture binding on the encoder; BindGroup is a no-op placeholder.
}

void ComputePassEncoder::setPipeline(std::shared_ptr<ComputePipeline> pipeline) {
    auto *data = static_cast<MetalComputeEncoderData *>(native);
    data->currentPipeline = static_cast<MTL::ComputePipelineState *>(pipeline->native);
    data->encoder->setComputePipelineState(data->currentPipeline);
}
