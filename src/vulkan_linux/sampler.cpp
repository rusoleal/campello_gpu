#include <campello_gpu/sampler.hpp>
#include "sampler_handle.hpp"

using namespace systems::leal::campello_gpu;

Sampler::Sampler(void *pd) {
    native = pd;
    
}

Sampler::~Sampler() {
    auto handle = (SamplerHandle *)native;

    vkDestroySampler(handle->device, handle->sampler, nullptr);

    delete handle;
    
}
