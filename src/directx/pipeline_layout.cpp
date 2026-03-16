#include "common.hpp"
#include <campello_gpu/pipeline_layout.hpp>

using namespace systems::leal::campello_gpu;

PipelineLayout::PipelineLayout(void* pd) : native(pd) {}

PipelineLayout::~PipelineLayout() {
    if (!native) return;
    auto* h = static_cast<PipelineLayoutHandle*>(native);
    if (h->rootSignature) h->rootSignature->Release();
    delete h;
}
