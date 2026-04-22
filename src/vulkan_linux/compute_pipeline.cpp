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
