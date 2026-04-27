#pragma once

#include <webgpu/webgpu.h>
#include <atomic>
#include <cstdint>
#include <campello_gpu/metrics.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/primitive_topology.hpp>
#include <campello_gpu/constants/front_face.hpp>
#include <campello_gpu/constants/cull_mode.hpp>
#include <campello_gpu/constants/compare_op.hpp>
#include <campello_gpu/constants/stencil_op.hpp>
#include <campello_gpu/constants/index_format.hpp>
#include <campello_gpu/constants/wrap_mode.hpp>
#include <campello_gpu/constants/filter_mode.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/shader_stage.hpp>
#include <campello_gpu/descriptors/fragment_descriptor.hpp>
#include <campello_gpu/descriptors/vertex_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>

namespace systems::leal::campello_gpu {

struct MipmapGenResources {
    WGPUShaderModule vsModule = nullptr;
    WGPUShaderModule fsModule = nullptr;
    WGPUBindGroupLayout bgl = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    WGPUSampler sampler = nullptr;
};

struct WebGPUDeviceData {
    WGPUInstance instance = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUSurface surface = nullptr;
    WGPUSwapChain swapchain = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
    uint32_t surfaceWidth = 0;
    uint32_t surfaceHeight = 0;
    MipmapGenResources mipmapGen;

    // Resource counters
    std::atomic<uint32_t> bufferCount{0};
    std::atomic<uint32_t> textureCount{0};
    std::atomic<uint32_t> renderPipelineCount{0};
    std::atomic<uint32_t> computePipelineCount{0};
    std::atomic<uint32_t> rayTracingPipelineCount{0};
    std::atomic<uint32_t> accelerationStructureCount{0};
    std::atomic<uint32_t> shaderModuleCount{0};
    std::atomic<uint32_t> samplerCount{0};
    std::atomic<uint32_t> bindGroupCount{0};
    std::atomic<uint32_t> bindGroupLayoutCount{0};
    std::atomic<uint32_t> pipelineLayoutCount{0};
    std::atomic<uint32_t> querySetCount{0};

    // Command stats
    std::atomic<uint64_t> commandsSubmitted{0};
    std::atomic<uint64_t> renderPasses{0};
    std::atomic<uint64_t> computePasses{0};
    std::atomic<uint64_t> rayTracingPasses{0};
    std::atomic<uint64_t> drawCalls{0};
    std::atomic<uint64_t> dispatchCalls{0};
    std::atomic<uint64_t> traceRaysCalls{0};
    std::atomic<uint64_t> copies{0};

    // Phase 2: Resource memory tracking (bytes)
    std::atomic<uint64_t> bufferBytes{0};
    std::atomic<uint64_t> textureBytes{0};
    std::atomic<uint64_t> accelerationStructureBytes{0};
    std::atomic<uint64_t> shaderModuleBytes{0};
    std::atomic<uint64_t> querySetBytes{0};

    // Phase 2: Peak memory tracking
    std::atomic<uint64_t> peakBufferBytes{0};
    std::atomic<uint64_t> peakTextureBytes{0};
    std::atomic<uint64_t> peakAccelerationStructureBytes{0};
    std::atomic<uint64_t> peakTotalBytes{0};

    // Phase 3: GPU pass timing (nanoseconds)
    std::atomic<uint64_t> renderPassTimeNs{0};
    std::atomic<uint64_t> computePassTimeNs{0};
    std::atomic<uint64_t> rayTracingPassTimeNs{0};
    std::atomic<uint32_t> renderPassSampleCount{0};
    std::atomic<uint32_t> computePassSampleCount{0};
    std::atomic<uint32_t> rayTracingPassSampleCount{0};

    // Phase 3: Memory budget and pressure management
    MemoryBudget memoryBudget;
    MemoryPressureCallback memoryPressureCallback;
    std::atomic<MemoryPressureLevel> lastPressureLevel{MemoryPressureLevel::Normal};
};

struct BufferHandle {
    WGPUBuffer buffer = nullptr;
    uint64_t size = 0;
    uint64_t allocatedSize = 0;
    WebGPUDeviceData* deviceData = nullptr;
};

struct TextureHandle {
    WGPUTexture texture = nullptr;
    WGPUTextureView defaultView = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
    uint32_t arrayLayers = 0;
    uint32_t mipLevels = 0;
    uint32_t samples = 0;
    PixelFormat pixelFormat = PixelFormat::invalid;
    TextureUsage usage = static_cast<TextureUsage>(0);
    TextureType textureType = TextureType::tt2d;
    uint64_t allocatedSize = 0;
    WebGPUDeviceData* deviceData = nullptr;
};

struct TextureViewHandle {
    WGPUTextureView view = nullptr;
    bool ownsView = true;
};

struct ShaderModuleHandle {
    WGPUShaderModule shaderModule = nullptr;
    uint64_t size = 0;
    WebGPUDeviceData* deviceData = nullptr;
};

struct RenderPipelineHandle {
    WGPURenderPipeline pipeline = nullptr;
    WGPUPipelineLayout layout = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct ComputePipelineHandle {
    WGPUComputePipeline pipeline = nullptr;
    WGPUPipelineLayout layout = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct BindGroupLayoutHandle {
    WGPUBindGroupLayout layout = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct PipelineLayoutHandle {
    WGPUPipelineLayout layout = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct SamplerHandle {
    WGPUSampler sampler = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct QuerySetHandle {
    WGPUQuerySet querySet = nullptr;
    WGPUQueryType queryType = WGPUQueryType_Occlusion;
    uint32_t count = 0;
    WebGPUDeviceData* deviceData = nullptr;
};

struct BindGroupHandle {
    WGPUBindGroup bindGroup = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct CommandEncoderHandle {
    WGPUCommandEncoder encoder = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
    // GPU timing resources — created in createCommandEncoder, transferred to CommandBuffer in finish()
    WGPUQuerySet timestampQuerySet = nullptr;
    WGPUBuffer timestampReadbackBuffer = nullptr;
};

struct CommandBufferHandle {
    WGPUCommandBuffer commandBuffer = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
    // GPU timing data — transferred from CommandEncoderHandle in finish()
    WGPUBuffer timestampReadbackBuffer = nullptr;
    uint64_t gpuStartTimestamp = 0;
    uint64_t gpuEndTimestamp = 0;
    bool hasTimingData = false;
};

struct RenderPassEncoderHandle {
    WGPURenderPassEncoder encoder = nullptr;
    WGPURenderPipeline currentPipeline = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct ComputePassEncoderHandle {
    WGPUComputePassEncoder encoder = nullptr;
    WGPUComputePipeline currentPipeline = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct FenceHandle {
    std::atomic<bool> signaled{false};
};

struct AccelerationStructureHandle {
    void* placeholder = nullptr;
};

struct RayTracingPipelineHandle {
    void* placeholder = nullptr;
};

struct RayTracingPassEncoderHandle {
    void* placeholder = nullptr;
};

// Format conversion helpers
WGPUTextureFormat toWGPUTextureFormat(PixelFormat format);
PixelFormat wgpuFormatToPixelFormat(WGPUTextureFormat format);
WGPUVertexFormat toWGPUVertexFormat(ComponentType comp, AccessorType acc, bool normalized);
WGPUPrimitiveTopology toWGPUPrimitiveTopology(PrimitiveTopology topology);
WGPUFrontFace toWGPUFrontFace(FrontFace face);
WGPUCullMode toWGPUCullMode(CullMode mode);
WGPUBlendFactor toWGPUBlendFactor(BlendFactor factor);
WGPUBlendOperation toWGPUBlendOperation(BlendOperation op);
WGPUCompareFunction toWGPUCompareFunction(CompareOp op);
WGPUStencilOperation toWGPUStencilOperation(StencilOp op);
WGPUIndexFormat toWGPUIndexFormat(IndexFormat format);
WGPUAddressMode toWGPUAddressMode(WrapMode mode);
WGPUFilterMode toWGPUFilterMode(FilterMode mode);
WGPUMipmapFilterMode toWGPUMipmapFilterMode(FilterMode mode);
WGPULoadOp toWGPULoadOp(LoadOp op);
WGPUStoreOp toWGPUStoreOp(StoreOp op);
WGPUTextureDimension toWGPUTextureDimension(TextureType type);
WGPUTextureViewDimension toWGPUTextureViewDimension(TextureType type);
WGPUTextureUsageFlags toWGPUTextureUsage(TextureUsage usage);
WGPUBufferUsageFlags toWGPUBufferUsage(BufferUsage usage);
WGPUShaderStageFlags toWGPUShaderStage(ShaderStage stage);
WGPUBufferBindingType toWGPUBindingType(EntryObjectType type, EntryObjectBufferType bufferType);
WGPUSamplerBindingType toWGPUSamplerBindingType(EntryObjectSamplerType type);
WGPUTextureSampleType toWGPUTextureSampleType(EntryObjectTextureType type);

} // namespace systems::leal::campello_gpu
