#include <campello_gpu/pipeline_layout.hpp>
#include "pipeline_layout_data.hpp"

using namespace systems::leal::campello_gpu;

// Metal uses implicit pipeline layout; this object is a no-op placeholder.
PipelineLayout::PipelineLayout(void *pd) : native(pd) {}

PipelineLayout::~PipelineLayout() {
    delete static_cast<MetalPipelineLayoutData*>(native);
}
