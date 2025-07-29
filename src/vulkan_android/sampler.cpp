#include <android/log.h>
#include <campello_gpu/sampler.hpp>
#include "sampler_handle.hpp"

using namespace systems::leal::campello_gpu;

Sampler::Sampler(void *pd) {
    native = pd;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","Sampler::Sampler()");
}

Sampler::~Sampler() {
    auto handle = (SamplerHandle *)native;

    vkDestroySampler(handle->device, handle->sampler, nullptr);

    delete handle;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","Sampler::~Sampler()");
}
