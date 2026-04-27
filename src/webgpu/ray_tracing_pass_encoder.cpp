#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/ray_tracing_pipeline.hpp>
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/bind_group.hpp>
#include "ray_tracing_pass_encoder_handle.hpp"

using namespace systems::leal::campello_gpu;

RayTracingPassEncoder::RayTracingPassEncoder(void* pd) {
    native = pd;
}

RayTracingPassEncoder::~RayTracingPassEncoder() {
    auto* handle = static_cast<RayTracingPassEncoderHandle*>(native);
    delete handle;
}

void RayTracingPassEncoder::end() {
}

void RayTracingPassEncoder::setPipeline(std::shared_ptr<RayTracingPipeline> pipeline) {
    (void)pipeline;
}

void RayTracingPassEncoder::setBindGroup(uint32_t index, std::shared_ptr<BindGroup> bindGroup,
                                          const std::vector<uint32_t>& dynamicOffsets,
                                          uint64_t dynamicOffsetsStart,
                                          uint64_t dynamicOffsetsLength) {
    (void)index;
    (void)bindGroup;
    (void)dynamicOffsets;
    (void)dynamicOffsetsStart;
    (void)dynamicOffsetsLength;
}

void RayTracingPassEncoder::traceRays(uint32_t width, uint32_t height, uint32_t depth) {
    (void)width;
    (void)height;
    (void)depth;
}
