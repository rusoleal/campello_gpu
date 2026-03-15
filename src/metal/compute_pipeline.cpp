#include "Metal.hpp"
#include <campello_gpu/compute_pipeline.hpp>

using namespace systems::leal::campello_gpu;

ComputePipeline::ComputePipeline(void *data) : native(data) {}

ComputePipeline::~ComputePipeline() {
    if (native != nullptr) {
        static_cast<MTL::ComputePipelineState *>(native)->release();
    }
}
