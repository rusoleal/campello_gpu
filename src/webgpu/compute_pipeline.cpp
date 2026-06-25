#include <campello_gpu/compute_pipeline.hpp>
#include "compute_pipeline_handle.hpp"

using namespace systems::leal::campello_gpu;

ComputePipeline::ComputePipeline(void* pd) {
    native = pd;
}

ComputePipeline::~ComputePipeline() {
    auto* handle = static_cast<ComputePipelineHandle*>(native);
    if (handle->deviceData) {
        handle->deviceData->computePipelineCount--;
    }
    if (handle->pipeline) {
        wgpuComputePipelineRelease(handle->pipeline);
    }
    delete handle;
}

WorkgroupSize ComputePipeline::getWorkgroupSize() const {
    return {1, 1, 1};
}
