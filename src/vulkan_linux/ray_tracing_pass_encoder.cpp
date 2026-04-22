#include <vulkan/vulkan.h>
#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/ray_tracing_pipeline.hpp>
#include <campello_gpu/bind_group.hpp>
#include "ray_tracing_pass_encoder_handle.hpp"
#include "ray_tracing_pipeline_handle.hpp"
#include "bind_group_handle.hpp"

using namespace systems::leal::campello_gpu;

RayTracingPassEncoder::RayTracingPassEncoder(void *pd) : native(pd) {}

RayTracingPassEncoder::~RayTracingPassEncoder() {
    delete (RayTracingPassEncoderHandle *)native;
}

void RayTracingPassEncoder::setPipeline(std::shared_ptr<RayTracingPipeline> pipeline) {
    // Deferred: ray tracing not yet implemented on Linux.
    (void)pipeline;
}

void RayTracingPassEncoder::setBindGroup(uint32_t index,
                                          std::shared_ptr<BindGroup> bindGroup,
                                          const std::vector<uint32_t> &dynamicOffsets,
                                          uint64_t dynamicOffsetsStart,
                                          uint64_t dynamicOffsetsLength) {
    // Deferred: ray tracing not yet implemented on Linux.
    (void)index;
    (void)bindGroup;
    (void)dynamicOffsets;
    (void)dynamicOffsetsStart;
    (void)dynamicOffsetsLength;
}

void RayTracingPassEncoder::traceRays(uint32_t width, uint32_t height, uint32_t depth) {
    // Deferred: ray tracing not yet implemented on Linux.
    (void)width;
    (void)height;
    (void)depth;
}

void RayTracingPassEncoder::end() {
    // No explicit end command for ray tracing passes in Vulkan KHR.
}
