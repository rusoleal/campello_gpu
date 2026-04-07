#include <android/log.h>
#include <vulkan/vulkan.h>
#include <campello_gpu/ray_tracing_pipeline.hpp>
#include "ray_tracing_pipeline_handle.hpp"

using namespace systems::leal::campello_gpu;

RayTracingPipeline::RayTracingPipeline(void *data) : native(data) {}

RayTracingPipeline::~RayTracingPipeline() {
    if (!native) return;
    auto *h = (RayTracingPipelineHandle *)native;
    if (h->sbtMemory  != VK_NULL_HANDLE) vkFreeMemory(h->device, h->sbtMemory, nullptr);
    if (h->sbtBuffer  != VK_NULL_HANDLE) vkDestroyBuffer(h->device, h->sbtBuffer, nullptr);
    if (h->pipeline   != VK_NULL_HANDLE) vkDestroyPipeline(h->device, h->pipeline, nullptr);
    delete h;
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "RayTracingPipeline::~RayTracingPipeline()");
}
