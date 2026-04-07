#include "Metal.hpp"
#include "ray_tracing_pipeline_handle.hpp"
#include <campello_gpu/ray_tracing_pipeline.hpp>

using namespace systems::leal::campello_gpu;

RayTracingPipeline::RayTracingPipeline(void *data) : native(data) {}

RayTracingPipeline::~RayTracingPipeline() {
    if (native != nullptr) {
        auto *d = static_cast<MetalRayTracingPipelineData *>(native);
        if (d->pipelineState)              d->pipelineState->release();
        if (d->intersectionFunctionTable)  d->intersectionFunctionTable->release();
        delete d;
    }
}
