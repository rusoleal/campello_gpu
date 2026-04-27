#include <campello_gpu/render_pipeline.hpp>
#include "render_pipeline_handle.hpp"

using namespace systems::leal::campello_gpu;

RenderPipeline::RenderPipeline(void* pd) {
    native = pd;
}

RenderPipeline::~RenderPipeline() {
    auto* handle = static_cast<RenderPipelineHandle*>(native);
    if (handle->deviceData) {
        handle->deviceData->renderPipelineCount--;
    }
    if (handle->pipeline) {
        wgpuRenderPipelineRelease(handle->pipeline);
    }
    delete handle;
}
