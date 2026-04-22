#include <campello_gpu/render_pipeline.hpp>
#include "render_pipeline_handle.hpp"

using namespace systems::leal::campello_gpu;

RenderPipeline::RenderPipeline(void *pd) {
    
    native = pd;
}

RenderPipeline::~RenderPipeline() {

    auto handle = (RenderPipelineHandle *)native;

    vkDestroyPipeline(handle->device, handle->pipeline, nullptr);
    if (handle->ownsPipelineLayout && handle->pipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(handle->device, handle->pipelineLayout, nullptr);

    delete handle;
    

}
