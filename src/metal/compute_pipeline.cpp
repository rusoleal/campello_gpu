#include "Metal.hpp"
#include <campello_gpu/compute_pipeline.hpp>

using namespace systems::leal::campello_gpu;

ComputePipeline::ComputePipeline(void *data) : native(data) {}

ComputePipeline::~ComputePipeline() {
    if (native != nullptr) {
        static_cast<MTL::ComputePipelineState *>(native)->release();
    }
}

WorkgroupSize ComputePipeline::getWorkgroupSize() const {
    if (native == nullptr)
        return {1, 1, 1};
    auto *pso = static_cast<MTL::ComputePipelineState *>(native);
    return {static_cast<uint32_t>(pso->threadExecutionWidth()), 1u, 1u};
}
