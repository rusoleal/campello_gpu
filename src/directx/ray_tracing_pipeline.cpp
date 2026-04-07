#define NOMINMAX
#include <d3d12.h>
#include "common.hpp"
#include <campello_gpu/ray_tracing_pipeline.hpp>

using namespace systems::leal::campello_gpu;

RayTracingPipeline::RayTracingPipeline(void* data) : native(data) {}

RayTracingPipeline::~RayTracingPipeline() {
    if (!native) return;
    auto* h = static_cast<RayTracingPipelineHandle*>(native);
    if (h->hitGroupTable) h->hitGroupTable->Release();
    if (h->missTable)     h->missTable->Release();
    if (h->rayGenTable)   h->rayGenTable->Release();
    if (h->rootSignature) h->rootSignature->Release();
    if (h->soProps)       h->soProps->Release();
    if (h->stateObject)   h->stateObject->Release();
    delete h;
}
