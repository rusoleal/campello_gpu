#include <campello_gpu/pipeline_layout.hpp>
#include "pipeline_layout_handle.hpp"

using namespace systems::leal::campello_gpu;

PipelineLayout::PipelineLayout(void *pd) {
    native = pd;
    
}

PipelineLayout::~PipelineLayout() {
    auto handle = (PipelineLayoutHandle *)native;

    vkDestroyPipelineLayout(handle->device, handle->layout, nullptr);

    delete handle;
    
}
