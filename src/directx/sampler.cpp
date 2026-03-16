#include "common.hpp"
#include <campello_gpu/sampler.hpp>

using namespace systems::leal::campello_gpu;

Sampler::Sampler(void* pd) : native(pd) {}

Sampler::~Sampler() {
    if (!native) return;
    delete static_cast<SamplerHandle*>(native);
}
