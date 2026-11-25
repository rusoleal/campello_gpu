// Ray tracing passes on WebGPU are compute passes running emulated ray
// tracing pipelines (src/webgpu/rt/rt_shader.cpp): traceRays() binds the
// launch size and dispatches one invocation per launch index.

#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/ray_tracing_pipeline.hpp>
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/bind_group.hpp>
#include "ray_tracing_pass_encoder_handle.hpp"
#include "rt/rt_internal.hpp"
#include <algorithm>

using namespace systems::leal::campello_gpu;

RayTracingPassEncoder::RayTracingPassEncoder(void* pd) {
    native = pd;
}

RayTracingPassEncoder::~RayTracingPassEncoder() {
    auto* handle = static_cast<RayTracingPassEncoderHandle*>(native);
    if (!handle) return;
    if (handle->encoder) {
        if (!handle->ended) wgpuComputePassEncoderEnd(handle->encoder);
        wgpuComputePassEncoderRelease(handle->encoder);
    }
    delete handle;
}

void RayTracingPassEncoder::end() {
    auto* handle = static_cast<RayTracingPassEncoderHandle*>(native);
    if (!handle || !handle->encoder || handle->ended) return;
    wgpuComputePassEncoderEnd(handle->encoder);
    handle->ended = true;
}

void RayTracingPassEncoder::setPipeline(std::shared_ptr<RayTracingPipeline> pipeline) {
    auto* handle = static_cast<RayTracingPassEncoderHandle*>(native);
    if (!handle || handle->ended || !pipeline) return;
    auto* p = static_cast<RayTracingPipelineHandle*>(pipeline->native);
    handle->pipeline = p;
    wgpuComputePassEncoderSetPipeline(handle->encoder, p->pipeline);
}

void RayTracingPassEncoder::setBindGroup(uint32_t index, std::shared_ptr<BindGroup> bindGroup,
                                          const std::vector<uint32_t>& dynamicOffsets,
                                          uint64_t dynamicOffsetsStart,
                                          uint64_t dynamicOffsetsLength) {
    auto* handle = static_cast<RayTracingPassEncoderHandle*>(native);
    if (!handle || handle->ended || !bindGroup) return;
    auto* bg = static_cast<BindGroupHandle*>(bindGroup->native);
    const uint32_t* offsets = dynamicOffsets.empty() ? nullptr : dynamicOffsets.data() + dynamicOffsetsStart;
    uint32_t count = static_cast<uint32_t>(dynamicOffsetsLength);
    if (count == 0 && !dynamicOffsets.empty()) {
        count = static_cast<uint32_t>(dynamicOffsets.size() - dynamicOffsetsStart);
    }
    wgpuComputePassEncoderSetBindGroup(handle->encoder, index, bg->bindGroup, count, offsets);
}

void RayTracingPassEncoder::traceRays(uint32_t width, uint32_t height, uint32_t depth) {
    auto* handle = static_cast<RayTracingPassEncoderHandle*>(native);
    if (!handle || handle->ended || !handle->pipeline || width == 0 || height == 0 || depth == 0) return;
    WebGPUDeviceData* dd = handle->deviceData;
    RayTracingState* rt = rayTracingState(dd);

    // The launch size, in a buffer of its own: a queue write lands before the
    // command buffer runs, and nothing else uses this buffer.
    const uint32_t size[4] = {width, height, depth, 0};
    WGPUBufferDescriptor bufDesc{};
    bufDesc.size = sizeof(size);
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(dd->device, &bufDesc);
    if (!buffer) return;
    wgpuQueueWriteBuffer(dd->queue, buffer, 0, size, sizeof(size));
    WGPUBindGroupEntry entry{};
    entry.binding = 0;
    entry.buffer = buffer;
    entry.size = sizeof(size);
    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.layout = rt->launchLayout;
    bgDesc.entryCount = 1;
    bgDesc.entries = &entry;
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(dd->device, &bgDesc);
    wgpuComputePassEncoderSetBindGroup(handle->encoder, handle->pipeline->launchGroup, bg, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(handle->encoder, std::min<uint32_t>((width + 7) / 8, 65535),
                                             std::min<uint32_t>((height + 7) / 8, 65535), std::min<uint32_t>(depth, 65535));
    wgpuBindGroupRelease(bg);
    wgpuBufferRelease(buffer);
    dd->traceRaysCalls++;
}
