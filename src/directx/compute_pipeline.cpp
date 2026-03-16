#include "common.hpp"
#include <campello_gpu/compute_pipeline.hpp>

using namespace systems::leal::campello_gpu;

ComputePipeline::ComputePipeline(void* data) : native(data) {}

ComputePipeline::~ComputePipeline() {
    if (!native) return;
    auto* h = static_cast<ComputePipelineHandle*>(native);
    if (h->pso)           h->pso->Release();
    if (h->rootSignature) h->rootSignature->Release();
    delete h;
}
