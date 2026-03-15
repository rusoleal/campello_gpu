#include "Metal.hpp"
#include <campello_gpu/sampler.hpp>

using namespace systems::leal::campello_gpu;

Sampler::Sampler(void *pd) : native(pd) {}

Sampler::~Sampler() {
    if (native != nullptr) {
        static_cast<MTL::SamplerState *>(native)->release();
    }
}
