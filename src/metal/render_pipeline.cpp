#include "Metal.hpp"
#include "render_pipeline_handle.hpp"
#include <campello_gpu/render_pipeline.hpp>

using namespace systems::leal::campello_gpu;

RenderPipeline::RenderPipeline(void *data) : native(data) {}

RenderPipeline::~RenderPipeline() {
    if (native != nullptr) {
        auto *data = static_cast<MetalRenderPipelineData *>(native);
        data->pipelineState->release();
        if (data->depthStencilState) data->depthStencilState->release();
        delete data;
    }
}
