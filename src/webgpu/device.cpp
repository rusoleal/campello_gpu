#include <cstring>
#include <vector>
#include <set>
#include <string>
#include <iostream>
#include <webgpu/webgpu.h>
#include <emscripten.h>
#include <emscripten/html5_webgpu.h>

#include "campello_gpu_config.h"
#include <campello_gpu/device.hpp>
#include <campello_gpu/adapter.hpp>
#include <campello_gpu/shader_module.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/pipeline_layout.hpp>
#include <campello_gpu/sampler.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/ray_tracing_pipeline.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/descriptors/render_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/compute_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/descriptors/sampler_descriptor.hpp>
#include <campello_gpu/descriptors/query_set_descriptor.hpp>
#include <campello_gpu/descriptors/bottom_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/top_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/ray_tracing_pipeline_descriptor.hpp>
#include <campello_gpu/constants/query_set_type.hpp>

#include "common.hpp"
#include "buffer_handle.hpp"
#include "texture_handle.hpp"
#include "texture_view_handle.hpp"
#include "shader_module_handle.hpp"
#include "render_pipeline_handle.hpp"
#include "compute_pipeline_handle.hpp"
#include "bind_group_layout_handle.hpp"
#include "pipeline_layout_handle.hpp"
#include "sampler_handle.hpp"
#include "query_set_handle.hpp"
#include "bind_group_handle.hpp"
#include "command_encoder_handle.hpp"
#include "command_buffer_handle.hpp"
#include "fence_handle.hpp"

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint64_t calculateTextureSize(uint32_t width, uint32_t height, uint32_t depth,
                                      uint32_t mipLevels, PixelFormat format) {
    uint64_t size = 0;
    uint32_t w = width, h = height, d = depth;
    if (isCompressedFormat(format)) {
        auto blockDim = getPixelFormatBlockDimensions(format);
        uint32_t blockBytes = getPixelFormatBlockBytes(format);
        for (uint32_t i = 0; i < mipLevels; ++i) {
            uint32_t blocksX = (w + blockDim.width - 1) / blockDim.width;
            uint32_t blocksY = (h + blockDim.height - 1) / blockDim.height;
            size += (uint64_t)blocksX * blocksY * d * blockBytes;
            if (w > 1) w /= 2;
            if (h > 1) h /= 2;
            if (d > 1) d /= 2;
        }
    } else {
        uint32_t bytesPerPixel = getPixelFormatSize(format) / 8;
        for (uint32_t i = 0; i < mipLevels; ++i) {
            size += (uint64_t)w * h * d * bytesPerPixel;
            if (w > 1) w /= 2;
            if (h > 1) h /= 2;
            if (d > 1) d /= 2;
        }
    }
    return size;
}

static void updatePeakMemory(WebGPUDeviceData* data) {
    uint64_t currentBuffer = data->bufferBytes.load();
    uint64_t currentTexture = data->textureBytes.load();
    uint64_t currentTotal = currentBuffer + currentTexture;

    uint64_t prev = data->peakBufferBytes.load();
    while (currentBuffer > prev && !data->peakBufferBytes.compare_exchange_weak(prev, currentBuffer)) {}

    prev = data->peakTextureBytes.load();
    while (currentTexture > prev && !data->peakTextureBytes.compare_exchange_weak(prev, currentTexture)) {}

    prev = data->peakTotalBytes.load();
    while (currentTotal > prev && !data->peakTotalBytes.compare_exchange_weak(prev, currentTotal)) {}
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

Device::Device(void* pd) {
    auto* deviceData = new WebGPUDeviceData();
    deviceData->device = static_cast<WGPUDevice>(pd);
    deviceData->queue = wgpuDeviceGetQueue(deviceData->device);
    native = deviceData;
}

Device::~Device() {
    if (native != nullptr) {
        auto* deviceData = static_cast<WebGPUDeviceData*>(native);
        if (deviceData->mipmapGen.pipelineLayout) {
            wgpuPipelineLayoutRelease(deviceData->mipmapGen.pipelineLayout);
        }
        if (deviceData->mipmapGen.bgl) {
            wgpuBindGroupLayoutRelease(deviceData->mipmapGen.bgl);
        }
        if (deviceData->mipmapGen.fsModule) {
            wgpuShaderModuleRelease(deviceData->mipmapGen.fsModule);
        }
        if (deviceData->mipmapGen.vsModule) {
            wgpuShaderModuleRelease(deviceData->mipmapGen.vsModule);
        }
        if (deviceData->mipmapGen.sampler) {
            wgpuSamplerRelease(deviceData->mipmapGen.sampler);
        }
        if (deviceData->swapchain) {
            wgpuSwapChainRelease(deviceData->swapchain);
        }
        if (deviceData->surface) {
            wgpuSurfaceRelease(deviceData->surface);
        }
        if (deviceData->queue) {
            wgpuQueueRelease(deviceData->queue);
        }
        if (deviceData->device) {
            wgpuDeviceRelease(deviceData->device);
        }
        if (deviceData->instance) {
            wgpuInstanceRelease(deviceData->instance);
        }
        delete deviceData;
    }
}

std::string Device::getName() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    WGPUAdapter adapter = nullptr;
    // webgpu.h does not expose wgpuDeviceGetAdapter in all versions;
    // fall back to a generic name.
    (void)adapter;
    return "WebGPU Device";
}

std::set<Feature> Device::getFeatures() {
    std::set<Feature> features;
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);

    features.insert(Feature::msaa32bit);
    features.insert(Feature::depth24Stencil8PixelFormat);

    if (deviceData->device) {
        if (wgpuDeviceHasFeature(deviceData->device, WGPUFeatureName_TextureCompressionBC))
            features.insert(Feature::bcTextureCompression);
        if (wgpuDeviceHasFeature(deviceData->device, WGPUFeatureName_TextureCompressionETC2))
            features.insert(Feature::etc2TextureCompression);
        if (wgpuDeviceHasFeature(deviceData->device, WGPUFeatureName_TextureCompressionASTC))
            features.insert(Feature::astcTextureCompression);
        // "shader-f16" is a standardized, shipping WebGPU feature (confirmed via
        // web search, not memory — has been available since early WebGPU
        // implementations) and its enum is present in Emscripten's bundled
        // webgpu.h, so it's queryable here.
        if (wgpuDeviceHasFeature(deviceData->device, WGPUFeatureName_ShaderF16))
            features.insert(Feature::fp16);
        // Feature::subgroupOperations intentionally NOT queried here: plain
        // "subgroups" (as opposed to subgroup-matrix, which is still unshipped
        // — see Adapter::getFeatures() above and TODO.md) did ship in Chrome 134
        // (Feb 2025) and is a real WGPUFeatureName in Dawn's own headers, but
        // the build-wasm CI job caught this session (2026-07-16): Emscripten
        // SDK 3.1.74's bundled webgpu.h does not define
        // WGPUFeatureName_Subgroups at all, so referencing it is a compile
        // error against this project's actual toolchain, not just a runtime
        // false-negative. Revisit once Emscripten's vendored header catches up.
    }

    return features;
}

std::vector<CooperativeMatrixProperties> Device::getCooperativeMatrixProperties() {
    // Not implemented -- WGSL's cooperative/subgroup-matrix proposal is still
    // in design discussion, unshipped (see Adapter::getFeatures() and
    // TODO.md). Feature::cooperativeMatrix is never set here either.
    return {};
}

DeviceMemoryInfo Device::getMemoryInfo() {
    DeviceMemoryInfo info{};
    // WebGPU does not expose detailed memory info.
    info.hasUnifiedMemory = true;
    return info;
}

ResourceCounters Device::getResourceCounters() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    ResourceCounters rc{};
    rc.bufferCount = deviceData->bufferCount.load();
    rc.textureCount = deviceData->textureCount.load();
    rc.renderPipelineCount = deviceData->renderPipelineCount.load();
    rc.computePipelineCount = deviceData->computePipelineCount.load();
    rc.rayTracingPipelineCount = deviceData->rayTracingPipelineCount.load();
    rc.accelerationStructureCount = deviceData->accelerationStructureCount.load();
    rc.shaderModuleCount = deviceData->shaderModuleCount.load();
    rc.samplerCount = deviceData->samplerCount.load();
    rc.bindGroupCount = deviceData->bindGroupCount.load();
    rc.bindGroupLayoutCount = deviceData->bindGroupLayoutCount.load();
    rc.pipelineLayoutCount = deviceData->pipelineLayoutCount.load();
    rc.querySetCount = deviceData->querySetCount.load();
    return rc;
}

CommandStats Device::getCommandStats() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    CommandStats cs{};
    cs.commandsSubmitted = deviceData->commandsSubmitted.load();
    cs.renderPasses = deviceData->renderPasses.load();
    cs.computePasses = deviceData->computePasses.load();
    cs.rayTracingPasses = deviceData->rayTracingPasses.load();
    cs.drawCalls = deviceData->drawCalls.load();
    cs.dispatchCalls = deviceData->dispatchCalls.load();
    cs.traceRaysCalls = deviceData->traceRaysCalls.load();
    cs.copies = deviceData->copies.load();
    return cs;
}

Metrics Device::getMetrics() {
    Metrics m{};
    m.deviceMemory = getMemoryInfo();
    m.resources = getResourceCounters();
    m.commands = getCommandStats();
    m.resourceMemory = getResourceMemoryStats();
    return m;
}

void Device::resetCommandStats() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    deviceData->commandsSubmitted.store(0);
    deviceData->renderPasses.store(0);
    deviceData->computePasses.store(0);
    deviceData->rayTracingPasses.store(0);
    deviceData->drawCalls.store(0);
    deviceData->dispatchCalls.store(0);
    deviceData->traceRaysCalls.store(0);
    deviceData->copies.store(0);
}

ResourceMemoryStats Device::getResourceMemoryStats() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    ResourceMemoryStats rms{};
    rms.bufferBytes = deviceData->bufferBytes.load();
    rms.textureBytes = deviceData->textureBytes.load();
    rms.accelerationStructureBytes = deviceData->accelerationStructureBytes.load();
    rms.shaderModuleBytes = deviceData->shaderModuleBytes.load();
    rms.querySetBytes = deviceData->querySetBytes.load();
    rms.peakBufferBytes = deviceData->peakBufferBytes.load();
    rms.peakTextureBytes = deviceData->peakTextureBytes.load();
    rms.peakAccelerationStructureBytes = deviceData->peakAccelerationStructureBytes.load();
    rms.peakTotalBytes = deviceData->peakTotalBytes.load();
    return rms;
}

void Device::resetPeakMemoryStats() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    uint64_t buf = deviceData->bufferBytes.load();
    uint64_t tex = deviceData->textureBytes.load();
    uint64_t as = deviceData->accelerationStructureBytes.load();
    deviceData->peakBufferBytes.store(buf);
    deviceData->peakTextureBytes.store(tex);
    deviceData->peakAccelerationStructureBytes.store(as);
    deviceData->peakTotalBytes.store(buf + tex + as);
}

PassPerformanceStats Device::getPassPerformanceStats() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    PassPerformanceStats pps{};
    pps.renderPassTimeNs = deviceData->renderPassTimeNs.load();
    pps.computePassTimeNs = deviceData->computePassTimeNs.load();
    pps.rayTracingPassTimeNs = deviceData->rayTracingPassTimeNs.load();
    pps.renderPassSampleCount = deviceData->renderPassSampleCount.load();
    pps.computePassSampleCount = deviceData->computePassSampleCount.load();
    pps.rayTracingPassSampleCount = deviceData->rayTracingPassSampleCount.load();
    return pps;
}

void Device::resetPassPerformanceStats() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    deviceData->renderPassTimeNs.store(0);
    deviceData->computePassTimeNs.store(0);
    deviceData->rayTracingPassTimeNs.store(0);
    deviceData->renderPassSampleCount.store(0);
    deviceData->computePassSampleCount.store(0);
    deviceData->rayTracingPassSampleCount.store(0);
}

MemoryPressureLevel Device::getMemoryPressureLevel() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    return deviceData->lastPressureLevel.load();
}

void Device::setMemoryBudget(const MemoryBudget& budget) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    deviceData->memoryBudget = budget;
}

MemoryBudget Device::getMemoryBudget() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    return deviceData->memoryBudget;
}

void Device::setMemoryPressureCallback(MemoryPressureCallback callback) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    deviceData->memoryPressureCallback = callback;
}

MemoryPressureLevel Device::checkMemoryPressure() {
    // WebGPU does not expose memory pressure queries.
    return MemoryPressureLevel::Normal;
}

MetricsWithTiming Device::getMetricsWithTiming() {
    MetricsWithTiming mwt{};
    mwt.deviceMemory = getMemoryInfo();
    mwt.resources = getResourceCounters();
    mwt.commands = getCommandStats();
    mwt.resourceMemory = getResourceMemoryStats();
    mwt.passPerformance = getPassPerformanceStats();
    return mwt;
}

std::shared_ptr<Texture> Device::createTexture(
    TextureType type, PixelFormat pixelFormat,
    uint32_t width, uint32_t height, uint32_t depth,
    uint32_t mipLevels, uint32_t samples,
    TextureUsage usageMode) {

    auto* deviceData = static_cast<WebGPUDeviceData*>(native);

    WGPUTextureDescriptor desc{};
    desc.dimension = toWGPUTextureDimension(type);
    desc.format = toWGPUTextureFormat(pixelFormat);
    desc.size.width = width;
    desc.size.height = height;
    desc.size.depthOrArrayLayers = depth;
    desc.mipLevelCount = mipLevels;
    desc.sampleCount = samples;
    desc.usage = toWGPUTextureUsage(usageMode);

    WGPUTexture texture = wgpuDeviceCreateTexture(deviceData->device, &desc);
    if (!texture) return nullptr;

    auto* handle = new TextureHandle();
    handle->texture = texture;
    handle->width = width;
    handle->height = height;
    handle->depth = depth;
    handle->arrayLayers = depth; // For array textures, depth is array layer count
    handle->mipLevels = mipLevels;
    handle->samples = samples;
    handle->pixelFormat = pixelFormat;
    handle->usage = usageMode;
    handle->textureType = type;
    handle->allocatedSize = calculateTextureSize(width, height, depth, mipLevels, pixelFormat);
    handle->deviceData = deviceData;

    // Create default view
    WGPUTextureViewDescriptor viewDesc{};
    viewDesc.format = desc.format;
    viewDesc.dimension = toWGPUTextureViewDimension(type);
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = mipLevels;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = (type == TextureType::tt3d) ? 1 : depth;
    viewDesc.aspect = WGPUTextureAspect_All;
    handle->defaultView = wgpuTextureCreateView(texture, &viewDesc);

    deviceData->textureCount++;
    deviceData->textureBytes.fetch_add(handle->allocatedSize);
    updatePeakMemory(deviceData);

    return std::shared_ptr<Texture>(new Texture(handle));
}

std::shared_ptr<Buffer> Device::createBuffer(uint64_t size, BufferUsage usage) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);

    WGPUBufferDescriptor desc{};
    desc.size = size;
    desc.usage = toWGPUBufferUsage(usage);
    desc.mappedAtCreation = false;

    WGPUBuffer buffer = wgpuDeviceCreateBuffer(deviceData->device, &desc);
    if (!buffer) return nullptr;

    auto* handle = new BufferHandle();
    handle->buffer = buffer;
    handle->size = size;
    handle->allocatedSize = size;
    handle->deviceData = deviceData;

    deviceData->bufferCount++;
    deviceData->bufferBytes.fetch_add(size);
    updatePeakMemory(deviceData);

    return std::shared_ptr<Buffer>(new Buffer(handle));
}

// createBuffer(uint64_t, BufferUsage, void*) is implemented in src/pi/utils.cpp

std::shared_ptr<ShaderModule> Device::createShaderModule(const uint8_t* buffer, uint64_t size) {
    // WebGPU backend expects WGSL source text, not binary.
    // If binary is passed, we cannot use it. Return nullptr.
    (void)buffer;
    (void)size;
    return nullptr;
}

std::shared_ptr<ShaderModule> Device::createShaderModule(const char* wgslSource) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);

    WGPUShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = wgslSource;

    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);

    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(deviceData->device, &desc);
    if (!shader) return nullptr;

    auto* handle = new ShaderModuleHandle();
    handle->shaderModule = shader;
    handle->size = strlen(wgslSource);
    handle->deviceData = deviceData;

    deviceData->shaderModuleCount++;
    deviceData->shaderModuleBytes.fetch_add(handle->size);

    return std::shared_ptr<ShaderModule>(new ShaderModule(handle));
}

std::shared_ptr<RenderPipeline> Device::createRenderPipeline(const RenderPipelineDescriptor& descriptor) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);

    if (!descriptor.vertex.module) return nullptr;

    // Vertex state
    auto* vertHandle = static_cast<ShaderModuleHandle*>(descriptor.vertex.module->native);
    WGPUVertexState vertexState{};
    vertexState.module = vertHandle->shaderModule;
    vertexState.entryPoint = descriptor.vertex.entryPoint.c_str();

    std::vector<WGPUVertexBufferLayout> vertexBuffers;
    std::vector<std::vector<WGPUVertexAttribute>> vertexAttributes;
    if (!descriptor.vertex.buffers.empty()) {
        vertexBuffers.reserve(descriptor.vertex.buffers.size());
        vertexAttributes.reserve(descriptor.vertex.buffers.size());
        for (const auto& layout : descriptor.vertex.buffers) {
            std::vector<WGPUVertexAttribute> attrs;
            attrs.reserve(layout.attributes.size());
            for (const auto& attr : layout.attributes) {
                WGPUVertexAttribute wgpuAttr{};
                wgpuAttr.format = toWGPUVertexFormat(attr.componentType, attr.accessorType, attr.normalized);
                wgpuAttr.offset = attr.offset;
                wgpuAttr.shaderLocation = attr.shaderLocation;
                attrs.push_back(wgpuAttr);
            }
            vertexAttributes.push_back(std::move(attrs));

            WGPUVertexBufferLayout bufLayout{};
            bufLayout.arrayStride = static_cast<uint64_t>(layout.arrayStride);
            bufLayout.stepMode = (layout.stepMode == StepMode::instance)
                                 ? WGPUVertexStepMode_Instance
                                 : WGPUVertexStepMode_Vertex;
            bufLayout.attributeCount = vertexAttributes.back().size();
            bufLayout.attributes = vertexAttributes.back().data();
            vertexBuffers.push_back(bufLayout);
        }
        vertexState.bufferCount = vertexBuffers.size();
        vertexState.buffers = vertexBuffers.data();
    }

    // Primitive state
    WGPUPrimitiveState primitiveState{};
    primitiveState.topology = toWGPUPrimitiveTopology(descriptor.topology);
    primitiveState.frontFace = toWGPUFrontFace(descriptor.frontFace);
    primitiveState.cullMode = toWGPUCullMode(descriptor.cullMode);

    // Depth/stencil state
    std::unique_ptr<WGPUDepthStencilState> depthStencilState;
    if (descriptor.depthStencil) {
        depthStencilState = std::make_unique<WGPUDepthStencilState>();
        *depthStencilState = WGPUDepthStencilState{};
        depthStencilState->format = toWGPUTextureFormat(descriptor.depthStencil->format);
        depthStencilState->depthWriteEnabled = descriptor.depthStencil->depthWriteEnabled;
        depthStencilState->depthCompare = toWGPUCompareFunction(descriptor.depthStencil->depthCompare);
        depthStencilState->depthBias = static_cast<int32_t>(descriptor.depthStencil->depthBias);
        depthStencilState->depthBiasSlopeScale = static_cast<float>(descriptor.depthStencil->depthBiasSlopeScale);
        depthStencilState->depthBiasClamp = static_cast<float>(descriptor.depthStencil->depthBiasClamp);
        depthStencilState->stencilReadMask = descriptor.depthStencil->stencilReadMask;
        depthStencilState->stencilWriteMask = descriptor.depthStencil->stencilWriteMask;

        if (descriptor.depthStencil->stencilFront) {
            depthStencilState->stencilFront.compare = toWGPUCompareFunction(descriptor.depthStencil->stencilFront->compare);
            depthStencilState->stencilFront.failOp = toWGPUStencilOperation(descriptor.depthStencil->stencilFront->failOp);
            depthStencilState->stencilFront.depthFailOp = toWGPUStencilOperation(descriptor.depthStencil->stencilFront->depthFailOp);
            depthStencilState->stencilFront.passOp = toWGPUStencilOperation(descriptor.depthStencil->stencilFront->passOp);
        }
        if (descriptor.depthStencil->stencilBack) {
            depthStencilState->stencilBack.compare = toWGPUCompareFunction(descriptor.depthStencil->stencilBack->compare);
            depthStencilState->stencilBack.failOp = toWGPUStencilOperation(descriptor.depthStencil->stencilBack->failOp);
            depthStencilState->stencilBack.depthFailOp = toWGPUStencilOperation(descriptor.depthStencil->stencilBack->depthFailOp);
            depthStencilState->stencilBack.passOp = toWGPUStencilOperation(descriptor.depthStencil->stencilBack->passOp);
        }
    }

    // Multisample state
    WGPUMultisampleState multisampleState{};
    multisampleState.count = 1;
    multisampleState.mask = 0xFFFFFFFF;

    // Fragment state
    std::unique_ptr<WGPUFragmentState> fragmentState;
    std::vector<WGPUColorTargetState> colorTargets;
    std::vector<std::unique_ptr<WGPUBlendState>> blendStates;
    if (descriptor.fragment && descriptor.fragment->module) {
        fragmentState = std::make_unique<WGPUFragmentState>();
        *fragmentState = WGPUFragmentState{};
        auto* fragHandle = static_cast<ShaderModuleHandle*>(descriptor.fragment->module->native);
        fragmentState->module = fragHandle->shaderModule;
        fragmentState->entryPoint = descriptor.fragment->entryPoint.c_str();

        colorTargets.reserve(descriptor.fragment->targets.size());
        blendStates.reserve(descriptor.fragment->targets.size());
        for (const auto& target : descriptor.fragment->targets) {
            WGPUColorTargetState ct{};
            ct.format = toWGPUTextureFormat(target.format);
            ct.writeMask = static_cast<WGPUColorWriteMaskFlags>(target.writeMask);
            if (target.blend) {
                auto blend = std::make_unique<WGPUBlendState>();
                blend->color.srcFactor = toWGPUBlendFactor(target.blend->color.srcFactor);
                blend->color.dstFactor = toWGPUBlendFactor(target.blend->color.dstFactor);
                blend->color.operation = toWGPUBlendOperation(target.blend->color.operation);
                blend->alpha.srcFactor = toWGPUBlendFactor(target.blend->alpha.srcFactor);
                blend->alpha.dstFactor = toWGPUBlendFactor(target.blend->alpha.dstFactor);
                blend->alpha.operation = toWGPUBlendOperation(target.blend->alpha.operation);
                ct.blend = blend.get();
                blendStates.push_back(std::move(blend));
            }
            colorTargets.push_back(ct);
        }
        fragmentState->targetCount = colorTargets.size();
        fragmentState->targets = colorTargets.data();
    }

    // Pipeline layout
    WGPUPipelineLayout layout = nullptr;
    if (descriptor.layout && descriptor.layout->native) {
        auto* plHandle = static_cast<PipelineLayoutHandle*>(descriptor.layout->native);
        layout = plHandle->layout;
    }

    WGPURenderPipelineDescriptor desc{};
    desc.vertex = vertexState;
    desc.primitive = primitiveState;
    if (depthStencilState) {
        desc.depthStencil = depthStencilState.get();
    }
    desc.multisample = multisampleState;
    if (fragmentState) {
        desc.fragment = fragmentState.get();
    }
    desc.layout = layout;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(deviceData->device, &desc);
    if (!pipeline) return nullptr;

    auto* handle = new RenderPipelineHandle();
    handle->pipeline = pipeline;
    handle->layout = layout;
    handle->deviceData = deviceData;

    deviceData->renderPipelineCount++;
    return std::shared_ptr<RenderPipeline>(new RenderPipeline(handle));
}

std::shared_ptr<ComputePipeline> Device::createComputePipeline(const ComputePipelineDescriptor& descriptor) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    if (!descriptor.compute.module) return nullptr;

    auto* compHandle = static_cast<ShaderModuleHandle*>(descriptor.compute.module->native);

    WGPUProgrammableStageDescriptor computeStage{};
    computeStage.module = compHandle->shaderModule;
    computeStage.entryPoint = descriptor.compute.entryPoint.c_str();

    WGPUPipelineLayout layout = nullptr;
    if (descriptor.layout && descriptor.layout->native) {
        auto* plHandle = static_cast<PipelineLayoutHandle*>(descriptor.layout->native);
        layout = plHandle->layout;
    }

    WGPUComputePipelineDescriptor desc{};
    desc.compute = computeStage;
    desc.layout = layout;

    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(deviceData->device, &desc);
    if (!pipeline) return nullptr;

    auto* handle = new ComputePipelineHandle();
    handle->pipeline = pipeline;
    handle->layout = layout;
    handle->deviceData = deviceData;

    deviceData->computePipelineCount++;
    return std::shared_ptr<ComputePipeline>(new ComputePipeline(handle));
}

std::shared_ptr<BindGroupLayout> Device::createBindGroupLayout(const BindGroupLayoutDescriptor& descriptor) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);

    std::vector<WGPUBindGroupLayoutEntry> entries;
    entries.reserve(descriptor.entries.size());

    for (const auto& entry : descriptor.entries) {
        WGPUBindGroupLayoutEntry wgpuEntry{};
        wgpuEntry.binding = entry.binding;
        wgpuEntry.visibility = toWGPUShaderStage(entry.visibility);

        switch (entry.type) {
            case EntryObjectType::buffer: {
                wgpuEntry.buffer.type = toWGPUBindingType(entry.type, entry.data.buffer.type);
                wgpuEntry.buffer.hasDynamicOffset = entry.data.buffer.hasDinamicOffaset;
                wgpuEntry.buffer.minBindingSize = entry.data.buffer.minBindingSize;
                break;
            }
            case EntryObjectType::sampler: {
                wgpuEntry.sampler.type = toWGPUSamplerBindingType(entry.data.sampler.type);
                break;
            }
            case EntryObjectType::texture: {
                wgpuEntry.texture.sampleType = toWGPUTextureSampleType(entry.data.texture.sampleType);
                wgpuEntry.texture.viewDimension = toWGPUTextureViewDimension(entry.data.texture.viewDimension);
                wgpuEntry.texture.multisampled = entry.data.texture.multisampled;
                break;
            }
            case EntryObjectType::accelerationStructure: {
                // WebGPU does not support acceleration structures in standard API.
                break;
            }
        }
        entries.push_back(wgpuEntry);
    }

    WGPUBindGroupLayoutDescriptor desc{};
    desc.entryCount = entries.size();
    desc.entries = entries.data();

    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(deviceData->device, &desc);
    if (!layout) return nullptr;

    auto* handle = new BindGroupLayoutHandle();
    handle->layout = layout;
    handle->deviceData = deviceData;

    deviceData->bindGroupLayoutCount++;
    return std::shared_ptr<BindGroupLayout>(new BindGroupLayout(handle));
}

std::shared_ptr<BindGroup> Device::createBindGroup(const BindGroupDescriptor& descriptor) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);

    WGPUBindGroupLayout layout = nullptr;
    if (descriptor.layout && descriptor.layout->native) {
        auto* bglHandle = static_cast<BindGroupLayoutHandle*>(descriptor.layout->native);
        layout = bglHandle->layout;
    }

    std::vector<WGPUBindGroupEntry> entries;
    entries.reserve(descriptor.entries.size());

    for (const auto& entry : descriptor.entries) {
        WGPUBindGroupEntry wgpuEntry{};
        wgpuEntry.binding = entry.binding;

        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, BufferBinding>) {
                wgpuEntry.buffer = static_cast<BufferHandle*>(arg.buffer->native)->buffer;
                wgpuEntry.offset = arg.offset;
                wgpuEntry.size = arg.size;
            } else if constexpr (std::is_same_v<T, std::shared_ptr<Texture>>) {
                auto* texHandle = static_cast<TextureHandle*>(arg->native);
                wgpuEntry.textureView = texHandle->defaultView;
            } else if constexpr (std::is_same_v<T, std::shared_ptr<Sampler>>) {
                auto* sampHandle = static_cast<SamplerHandle*>(arg->native);
                wgpuEntry.sampler = sampHandle->sampler;
            } else if constexpr (std::is_same_v<T, std::shared_ptr<AccelerationStructure>>) {
                // WebGPU does not support acceleration structures.
            }
        }, entry.resource);

        entries.push_back(wgpuEntry);
    }

    WGPUBindGroupDescriptor desc{};
    desc.layout = layout;
    desc.entryCount = entries.size();
    desc.entries = entries.data();

    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(deviceData->device, &desc);
    if (!bindGroup) return nullptr;

    auto* handle = new BindGroupHandle();
    handle->bindGroup = bindGroup;
    handle->deviceData = deviceData;

    deviceData->bindGroupCount++;
    return std::shared_ptr<BindGroup>(new BindGroup(handle));
}

std::shared_ptr<PipelineLayout> Device::createPipelineLayout(const PipelineLayoutDescriptor& descriptor) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);

    std::vector<WGPUBindGroupLayout> layouts;
    layouts.reserve(descriptor.bindGroupLayouts.size());
    for (const auto& layout : descriptor.bindGroupLayouts) {
        if (layout) {
            auto* h = static_cast<BindGroupLayoutHandle*>(layout->native);
            layouts.push_back(h->layout);
        } else {
            layouts.push_back(nullptr);
        }
    }

    WGPUPipelineLayoutDescriptor desc{};
    desc.bindGroupLayoutCount = layouts.size();
    desc.bindGroupLayouts = layouts.data();

    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(deviceData->device, &desc);
    if (!layout) return nullptr;

    auto* handle = new PipelineLayoutHandle();
    handle->layout = layout;
    handle->deviceData = deviceData;

    deviceData->pipelineLayoutCount++;
    return std::shared_ptr<PipelineLayout>(new PipelineLayout(handle));
}

std::shared_ptr<Sampler> Device::createSampler(const SamplerDescriptor& descriptor) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);

    WGPUSamplerDescriptor desc{};
    desc.addressModeU = toWGPUAddressMode(descriptor.addressModeU);
    desc.addressModeV = toWGPUAddressMode(descriptor.addressModeV);
    desc.addressModeW = toWGPUAddressMode(descriptor.addressModeW);
    desc.magFilter = toWGPUFilterMode(descriptor.magFilter);
    desc.minFilter = toWGPUFilterMode(descriptor.minFilter);
    desc.mipmapFilter = toWGPUMipmapFilterMode(descriptor.minFilter);
    desc.lodMinClamp = static_cast<float>(descriptor.lodMinClamp);
    desc.lodMaxClamp = static_cast<float>(descriptor.lodMaxClamp);
    desc.maxAnisotropy = static_cast<uint16_t>(descriptor.maxAnisotropy);
    if (descriptor.compare.has_value()) {
        desc.compare = toWGPUCompareFunction(descriptor.compare.value());
    }

    WGPUSampler sampler = wgpuDeviceCreateSampler(deviceData->device, &desc);
    if (!sampler) return nullptr;

    auto* handle = new SamplerHandle();
    handle->sampler = sampler;
    handle->deviceData = deviceData;

    deviceData->samplerCount++;
    return std::shared_ptr<Sampler>(new Sampler(handle));
}

std::shared_ptr<QuerySet> Device::createQuerySet(const QuerySetDescriptor& descriptor) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);

    WGPUQueryType type = (descriptor.type == QuerySetType::occlusion)
                         ? WGPUQueryType_Occlusion
                         : WGPUQueryType_Timestamp;

    WGPUQuerySetDescriptor desc{};
    desc.type = type;
    desc.count = descriptor.count;

    WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(deviceData->device, &desc);
    if (!querySet) return nullptr;

    auto* handle = new QuerySetHandle();
    handle->querySet = querySet;
    handle->queryType = type;
    handle->count = descriptor.count;
    handle->deviceData = deviceData;

    deviceData->querySetCount++;
    return std::shared_ptr<QuerySet>(new QuerySet(handle));
}

std::shared_ptr<CommandEncoder> Device::createCommandEncoder() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);

    WGPUCommandEncoderDescriptor desc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(deviceData->device, &desc);
    if (!encoder) return nullptr;

    auto* handle = new CommandEncoderHandle();
    handle->encoder = encoder;
    handle->deviceData = deviceData;

    // Create timestamp query set and readback buffer for GPU timing
    WGPUQuerySetDescriptor qsDesc{};
    qsDesc.type = WGPUQueryType_Timestamp;
    qsDesc.count = 2;  // start and end
    handle->timestampQuerySet = wgpuDeviceCreateQuerySet(deviceData->device, &qsDesc);

    if (handle->timestampQuerySet) {
        WGPUBufferDescriptor bufDesc{};
        bufDesc.size = 2 * sizeof(uint64_t);
        bufDesc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_MapRead;
        handle->timestampReadbackBuffer = wgpuDeviceCreateBuffer(deviceData->device, &bufDesc);

        if (handle->timestampReadbackBuffer) {
            wgpuCommandEncoderWriteTimestamp(encoder, handle->timestampQuerySet, 0);
        } else {
            wgpuQuerySetRelease(handle->timestampQuerySet);
            handle->timestampQuerySet = nullptr;
        }
    }

    return std::shared_ptr<CommandEncoder>(new CommandEncoder(handle));
}

std::shared_ptr<AccelerationStructure> Device::createBottomLevelAccelerationStructure(
    const BottomLevelAccelerationStructureDescriptor& descriptor) {
    (void)descriptor;
    return nullptr; // Ray tracing not supported in WebGPU
}

std::shared_ptr<AccelerationStructure> Device::createTopLevelAccelerationStructure(
    const TopLevelAccelerationStructureDescriptor& descriptor) {
    (void)descriptor;
    return nullptr; // Ray tracing not supported in WebGPU
}

std::shared_ptr<RayTracingPipeline> Device::createRayTracingPipeline(
    const RayTracingPipelineDescriptor& descriptor) {
    (void)descriptor;
    return nullptr; // Ray tracing not supported in WebGPU
}

void Device::submit(std::shared_ptr<CommandBuffer> commandBuffer) {
    if (!commandBuffer || !commandBuffer->native) return;
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    auto* cbHandle = static_cast<CommandBufferHandle*>(commandBuffer->native);

    WGPUCommandBuffer buffers[] = { cbHandle->commandBuffer };
    wgpuQueueSubmit(deviceData->queue, 1, buffers);
    deviceData->commandsSubmitted++;

    // Present swapchain if configured
    if (deviceData->swapchain) {
        wgpuSwapChainPresent(deviceData->swapchain);
    }
}

void Device::submit(std::shared_ptr<CommandBuffer> commandBuffer,
                    std::shared_ptr<Fence> signalFence) {
    submit(commandBuffer);
    if (signalFence && signalFence->native) {
        auto* fenceHandle = static_cast<FenceHandle*>(signalFence->native);
        fenceHandle->signaled.store(true);
    }
}

void Device::scheduleNextPresent(void* nativeDrawable) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    (void)nativeDrawable;
    // Presentation is handled automatically in submit() for WebGPU
    (void)deviceData;
}

void Device::waitForIdle() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    struct DoneCtx { bool done = false; };
    DoneCtx ctx;
    wgpuQueueOnSubmittedWorkDone(deviceData->queue,
        [](WGPUQueueWorkDoneStatus, void* userdata) {
            static_cast<DoneCtx*>(userdata)->done = true;
        }, &ctx);
    while (!ctx.done) {
        emscripten_sleep(1);
    }
}

std::shared_ptr<Fence> Device::createFence() {
    auto* handle = new FenceHandle();
    handle->signaled.store(false);
    return std::shared_ptr<Fence>(new Fence(handle));
}

std::shared_ptr<Device> Device::createDefaultDevice(void* pd) {
    WGPUDevice device = emscripten_webgpu_get_device();
    if (!device) return nullptr;

    auto dev = std::shared_ptr<Device>(new Device(device));
    auto* deviceData = static_cast<WebGPUDeviceData*>(dev->native);

    // Create surface and swapchain if platform handle (canvas selector) is provided
    if (pd != nullptr) {
        const char* canvasSelector = static_cast<const char*>(pd);

        WGPUInstance instance = wgpuCreateInstance(nullptr);
        deviceData->instance = instance;

        WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc{};
        canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
        canvasDesc.selector = canvasSelector;

        WGPUSurfaceDescriptor surfaceDesc{};
        surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct const*>(&canvasDesc);

        deviceData->surface = wgpuInstanceCreateSurface(instance, &surfaceDesc);

        WGPUSwapChainDescriptor swapDesc{};
        swapDesc.usage = WGPUTextureUsage_RenderAttachment;
        swapDesc.format = deviceData->surfaceFormat;
        swapDesc.width = 800;
        swapDesc.height = 600;
        swapDesc.presentMode = WGPUPresentMode_Fifo;
        deviceData->swapchain = wgpuDeviceCreateSwapChain(device, deviceData->surface, &swapDesc);
        deviceData->surfaceWidth = 800;
        deviceData->surfaceHeight = 600;
    }

    return dev;
}

std::shared_ptr<Device> Device::createDevice(std::shared_ptr<Adapter> adapter, void* pd) {
    // Emscripten provides a single default device; ignore adapter parameter.
    (void)adapter;
    return createDefaultDevice(pd);
}

std::vector<std::shared_ptr<Adapter>> Device::getAdapters() {
    std::vector<std::shared_ptr<Adapter>> adapters;
    // WebGPU via Emscripten does not expose adapter enumeration.
    // Return an empty list; consumers should use createDefaultDevice().
    return adapters;
}

std::string Device::getEngineVersion() {
    return std::to_string(campello_gpu_VERSION_MAJOR) + "." +
           std::to_string(campello_gpu_VERSION_MINOR) + "." +
           std::to_string(campello_gpu_VERSION_PATCH);
}

std::shared_ptr<TextureView> Device::getSwapchainTextureView() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    if (!deviceData->swapchain) return nullptr;

    WGPUTextureView view = wgpuSwapChainGetCurrentTextureView(deviceData->swapchain);
    if (!view) return nullptr;

    auto* handle = new TextureViewHandle();
    handle->view = view;
    handle->ownsView = true;
    return std::shared_ptr<TextureView>(new TextureView(handle));
}

std::string systems::leal::campello_gpu::getVersion() {
    return Device::getEngineVersion();
}
