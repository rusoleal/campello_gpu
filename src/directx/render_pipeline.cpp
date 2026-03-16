#include "common.hpp"
#include <campello_gpu/render_pipeline.hpp>

using namespace systems::leal::campello_gpu;

RenderPipeline::RenderPipeline(void* data) : native(data) {}

RenderPipeline::~RenderPipeline() {
    if (!native) return;
    auto* h = static_cast<RenderPipelineHandle*>(native);
    if (h->pso)           h->pso->Release();
    if (h->rootSignature) h->rootSignature->Release();
    delete h;
}
