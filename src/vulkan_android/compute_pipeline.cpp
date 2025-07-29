#include <android/log.h>
#include <campello_gpu/compute_pipeline.hpp>
#include "compute_pipeline_handle.hpp"

using namespace systems::leal::campello_gpu;

ComputePipeline::ComputePipeline(void *pd) {
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","ComputePipeline::ComputePipeline()");
    native = pd;
}

ComputePipeline::~ComputePipeline() {

    auto handle = (ComputePipelineHandle *)native;

    vkDestroyPipeline(handle->device, handle->pipeline, nullptr);

    delete handle;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","ComputePipeline::~ComputePipeline()");

}
