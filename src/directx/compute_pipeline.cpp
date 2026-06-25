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

WorkgroupSize ComputePipeline::getWorkgroupSize() const {
    // DirectX 12 respects the [numthreads(x,y,z)] declared in HLSL.
    // campello_nn's shaders currently declare [numthreads(1,1,1)].
    return {1, 1, 1};
}
