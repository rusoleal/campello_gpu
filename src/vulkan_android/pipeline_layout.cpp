#include <android/log.h>
#include <campello_gpu/pipeline_layout.hpp>
#include "pipeline_layout_handle.hpp"

using namespace systems::leal::campello_gpu;

PipelineLayout::PipelineLayout(void *pd) {
    native = pd;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","PipelineLayout::PipelineLayout()");
}

PipelineLayout::~PipelineLayout() {
    auto handle = (PipelineLayoutHandle *)native;

    vkDestroyPipelineLayout(handle->device, handle->layout, nullptr);

    delete handle;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","PipelineLayout::~PipelineLayout()");
}
