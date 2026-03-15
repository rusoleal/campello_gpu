#include "Metal.hpp"
#include <campello_gpu/render_pipeline.hpp>

using namespace systems::leal::campello_gpu;

RenderPipeline::RenderPipeline(void *data) : native(data) {}

RenderPipeline::~RenderPipeline() {
    if (native != nullptr) {
        static_cast<MTL::RenderPipelineState *>(native)->release();
    }
}
