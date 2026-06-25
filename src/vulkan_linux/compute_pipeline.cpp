#include <campello_gpu/compute_pipeline.hpp>
#include "compute_pipeline_handle.hpp"

using namespace systems::leal::campello_gpu;

ComputePipeline::ComputePipeline(void *pd) {
    
    native = pd;
}

ComputePipeline::~ComputePipeline() {

    auto handle = (ComputePipelineHandle *)native;

    vkDestroyPipeline(handle->device, handle->pipeline, nullptr);
    vkDestroyPipelineLayout(handle->device, handle->pipelineLayout, nullptr);

    delete handle;
    

}

WorkgroupSize ComputePipeline::getWorkgroupSize() const {
    // Vulkan respects the SPIR-V local_size declaration; campello_nn's shaders
    // currently declare local_size_x = 1 for correctness-first dispatches.
    return {1, 1, 1};
}
