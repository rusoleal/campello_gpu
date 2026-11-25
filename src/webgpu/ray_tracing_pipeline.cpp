#include <campello_gpu/ray_tracing_pipeline.hpp>
#include "ray_tracing_pipeline_handle.hpp"

using namespace systems::leal::campello_gpu;

RayTracingPipeline::RayTracingPipeline(void* pd) {
    native = pd;
}

RayTracingPipeline::~RayTracingPipeline() {
    auto* handle = static_cast<RayTracingPipelineHandle*>(native);
    if (!handle) return;
    if (handle->pipeline) wgpuComputePipelineRelease(handle->pipeline);
    if (handle->deviceData) handle->deviceData->rayTracingPipelineCount--;
    delete handle;
}
