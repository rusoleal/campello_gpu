#include <campello_gpu/sampler.hpp>
#include "sampler_handle.hpp"

using namespace systems::leal::campello_gpu;

Sampler::Sampler(void* pd) {
    native = pd;
}

Sampler::~Sampler() {
    auto* handle = static_cast<SamplerHandle*>(native);
    if (handle->deviceData) {
        handle->deviceData->samplerCount--;
    }
    if (handle->sampler) {
        wgpuSamplerRelease(handle->sampler);
    }
    delete handle;
}
