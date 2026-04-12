#include "Metal.hpp"
#include "ray_tracing_pipeline_handle.hpp"
#include "acceleration_structure_handle.hpp"
#include "bind_group_data.hpp"
#include "buffer_handle.hpp"
#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/ray_tracing_pipeline.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/sampler.hpp>
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>

using namespace systems::leal::campello_gpu;

// Internal state: compute encoder + current pipeline metadata for thread dispatch.
struct MetalRayTracingEncoderData {
    MTL::ComputeCommandEncoder *encoder;
    NS::UInteger                threadExecutionWidth         = 8;
    NS::UInteger                maxTotalThreadsPerThreadgroup = 64;

    explicit MetalRayTracingEncoderData(MTL::ComputeCommandEncoder *enc)
        : encoder(enc) {}
};

RayTracingPassEncoder::RayTracingPassEncoder(void *pd) {
    auto *enc = static_cast<MTL::ComputeCommandEncoder *>(pd);
    enc->retain(); // computeCommandEncoder() is autoreleased
    native = new MetalRayTracingEncoderData(enc);
}

RayTracingPassEncoder::~RayTracingPassEncoder() {
    if (native != nullptr) {
        auto *d = static_cast<MetalRayTracingEncoderData *>(native);
        d->encoder->release();
        delete d;
    }
}

void RayTracingPassEncoder::setPipeline(std::shared_ptr<RayTracingPipeline> pipeline) {
    auto *d  = static_cast<MetalRayTracingEncoderData *>(native);
    auto *pd = static_cast<MetalRayTracingPipelineData *>(pipeline->native);

    d->encoder->setComputePipelineState(pd->pipelineState);
    d->threadExecutionWidth          = pd->threadExecutionWidth;
    d->maxTotalThreadsPerThreadgroup = pd->maxTotalThreadsPerThreadgroup;

    // Bind the intersection function table at buffer index 0 if present.
    if (pd->intersectionFunctionTable)
        d->encoder->setIntersectionFunctionTable(pd->intersectionFunctionTable, 0);
}

void RayTracingPassEncoder::setBindGroup(uint32_t index,
                                         std::shared_ptr<BindGroup> bindGroup,
                                         const std::vector<uint32_t> &dynamicOffsets,
                                         uint64_t dynamicOffsetsStart,
                                         uint64_t dynamicOffsetsLength) {
    (void)index; (void)dynamicOffsets; (void)dynamicOffsetsStart; (void)dynamicOffsetsLength;
    if (!native) return;
    if (!bindGroup) return;
    auto *enc    = static_cast<MetalRayTracingEncoderData *>(native)->encoder;
    if (!enc) return;
    auto *bgData = static_cast<MetalBindGroupData *>(bindGroup->native);
    if (!bgData) return;

    for (const auto &entry : bgData->entries) {
        if (std::holds_alternative<std::shared_ptr<AccelerationStructure>>(entry.resource)) {
            auto *asData = static_cast<MetalAccelerationStructureData *>(
                std::get<std::shared_ptr<AccelerationStructure>>(entry.resource)->native);
            if (asData) enc->setAccelerationStructure(asData->accelerationStructure, entry.binding);
        } else if (std::holds_alternative<std::shared_ptr<Texture>>(entry.resource)) {
            auto *tex = static_cast<MTL::Texture *>(
                std::get<std::shared_ptr<Texture>>(entry.resource)->native);
            enc->setTexture(tex, entry.binding);
        } else if (std::holds_alternative<std::shared_ptr<Sampler>>(entry.resource)) {
            auto *samp = static_cast<MTL::SamplerState *>(
                std::get<std::shared_ptr<Sampler>>(entry.resource)->native);
            enc->setSamplerState(samp, entry.binding);
        } else if (std::holds_alternative<BufferBinding>(entry.resource)) {
            const auto &bb  = std::get<BufferBinding>(entry.resource);
            auto *bufHandle = static_cast<MetalBufferHandle *>(bb.buffer->native);
            auto       *buf = bufHandle->buffer;
            enc->setBuffer(buf, bb.offset, entry.binding);
        }
    }
}

void RayTracingPassEncoder::traceRays(uint32_t width, uint32_t height, uint32_t depth) {
    auto *d = static_cast<MetalRayTracingEncoderData *>(native);

    // Choose a 2D threadgroup size that fills a SIMD group width-first.
    NS::UInteger tgW = d->threadExecutionWidth;
    NS::UInteger tgH = d->maxTotalThreadsPerThreadgroup / tgW;
    if (tgH < 1) tgH = 1;

    d->encoder->dispatchThreads(
        MTL::Size::Make(width, height, depth),
        MTL::Size::Make(tgW, tgH, 1));
}

void RayTracingPassEncoder::end() {
    static_cast<MetalRayTracingEncoderData *>(native)->encoder->endEncoding();
}
