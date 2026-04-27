#include <campello_gpu/pipeline_layout.hpp>
#include "pipeline_layout_handle.hpp"

using namespace systems::leal::campello_gpu;

PipelineLayout::PipelineLayout(void* pd) {
    native = pd;
}

PipelineLayout::~PipelineLayout() {
    auto* handle = static_cast<PipelineLayoutHandle*>(native);
    if (handle->deviceData) {
        handle->deviceData->pipelineLayoutCount--;
    }
    if (handle->layout) {
        wgpuPipelineLayoutRelease(handle->layout);
    }
    delete handle;
}
