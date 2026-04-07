#define NOMINMAX
#include <d3d12.h>
#include "common.hpp"
#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/ray_tracing_pipeline.hpp>
#include <campello_gpu/bind_group.hpp>

using namespace systems::leal::campello_gpu;

RayTracingPassEncoder::RayTracingPassEncoder(void* pd) : native(pd) {}

RayTracingPassEncoder::~RayTracingPassEncoder() {
    if (!native) return;
    auto* h = static_cast<RayTracingPassEncoderHandle*>(native);
    if (h->cmdList4) h->cmdList4->Release();
    delete h;
}

void RayTracingPassEncoder::setPipeline(std::shared_ptr<RayTracingPipeline> pipeline) {
    if (!native || !pipeline || !pipeline->native) return;
    auto* h  = static_cast<RayTracingPassEncoderHandle*>(native);
    auto* ph = static_cast<RayTracingPipelineHandle*>(pipeline->native);
    h->cmdList4->SetPipelineState1(ph->stateObject);
    h->cmdList4->SetComputeRootSignature(ph->rootSignature);
    h->dispatchDesc = ph->dispatchDesc;
}

void RayTracingPassEncoder::setBindGroup(uint32_t index,
                                          std::shared_ptr<BindGroup> bindGroup,
                                          const std::vector<uint32_t>& /*dynamicOffsets*/,
                                          uint64_t /*dynamicOffsetsStart*/,
                                          uint64_t /*dynamicOffsetsLength*/) {
    if (!native || !bindGroup || !bindGroup->native) return;
    auto* h  = static_cast<RayTracingPassEncoderHandle*>(native);
    auto* bg = static_cast<BindGroupHandle*>(bindGroup->native);
    if (bg->gpuHandle.ptr != 0)
        h->cmdList4->SetComputeRootDescriptorTable(index, bg->gpuHandle);
    if (bg->samplerHandle.ptr != 0)
        h->cmdList4->SetComputeRootDescriptorTable(index + 1, bg->samplerHandle);
}

void RayTracingPassEncoder::traceRays(uint32_t width, uint32_t height, uint32_t depth) {
    if (!native) return;
    auto* h = static_cast<RayTracingPassEncoderHandle*>(native);
    D3D12_DISPATCH_RAYS_DESC desc = h->dispatchDesc;
    desc.Width  = width;
    desc.Height = height;
    desc.Depth  = depth;
    h->cmdList4->DispatchRays(&desc);
}

void RayTracingPassEncoder::end() {
    // No explicit end command needed for DXR.
}
