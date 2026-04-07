#include <android/log.h>
#include <vulkan/vulkan.h>
#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/ray_tracing_pipeline.hpp>
#include <campello_gpu/bind_group.hpp>
#include "ray_tracing_pass_encoder_handle.hpp"
#include "ray_tracing_pipeline_handle.hpp"
#include "bind_group_handle.hpp"

using namespace systems::leal::campello_gpu;

// Defined in device.cpp
extern PFN_vkCmdTraceRaysKHR pfnCmdTraceRaysKHR;

RayTracingPassEncoder::RayTracingPassEncoder(void *pd) : native(pd) {}

RayTracingPassEncoder::~RayTracingPassEncoder() {
    delete (RayTracingPassEncoderHandle *)native;
}

void RayTracingPassEncoder::setPipeline(std::shared_ptr<RayTracingPipeline> pipeline) {
    auto *h  = (RayTracingPassEncoderHandle *)native;
    auto *ph = (RayTracingPipelineHandle *)pipeline->native;

    vkCmdBindPipeline(h->commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, ph->pipeline);
    h->currentPipelineLayout = ph->pipelineLayout;
    h->rgenRegion = ph->rgenRegion;
    h->missRegion = ph->missRegion;
    h->hitRegion  = ph->hitRegion;
    h->callRegion = ph->callRegion;
}

void RayTracingPassEncoder::setBindGroup(uint32_t index,
                                          std::shared_ptr<BindGroup> bindGroup,
                                          const std::vector<uint32_t> &dynamicOffsets,
                                          uint64_t dynamicOffsetsStart,
                                          uint64_t dynamicOffsetsLength) {
    auto *h  = (RayTracingPassEncoderHandle *)native;
    if (!h->currentPipelineLayout) return;
    auto *bg = (BindGroupHandle *)bindGroup->native;

    uint32_t dynCount = (dynamicOffsetsLength > 0)
        ? (uint32_t)dynamicOffsetsLength
        : 0;
    const uint32_t *dynData = (dynCount > 0 && dynamicOffsetsStart < dynamicOffsets.size())
        ? dynamicOffsets.data() + dynamicOffsetsStart
        : nullptr;

    vkCmdBindDescriptorSets(h->commandBuffer,
                            VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            h->currentPipelineLayout,
                            index, 1, &bg->descriptorSet,
                            dynCount, dynData);
}

void RayTracingPassEncoder::traceRays(uint32_t width, uint32_t height, uint32_t depth) {
    auto *h = (RayTracingPassEncoderHandle *)native;
    if (!pfnCmdTraceRaysKHR) return;
    pfnCmdTraceRaysKHR(h->commandBuffer,
                       &h->rgenRegion, &h->missRegion, &h->hitRegion, &h->callRegion,
                       width, height, depth);
}

void RayTracingPassEncoder::end() {
    // No explicit end command for ray tracing passes in Vulkan KHR.
}
