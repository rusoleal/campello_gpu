#include <cstring>
#include <vector>
#include <set>
#include <string>
#include <iostream>
#include <webgpu/webgpu.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdio>
#include <emscripten.h>
#include <emscripten/html5.h>

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
#include <campello_gpu/validation_diagnostics.hpp>

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

// Defined in campello_gpu_web.js: publishes Module.campelloGpu.
extern "C" void campello_gpu_web_init(void);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// `depth` is the depth of a 3D texture or the layer count of an array/cube;
// only a 3D texture's depth shrinks with each mip level.
static uint64_t calculateTextureSize(uint32_t width, uint32_t height, uint32_t depth,
                                      uint32_t mipLevels, PixelFormat format, bool is3D) {
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
            if (is3D && d > 1) d /= 2;
        }
    } else {
        uint32_t bytesPerPixel = getPixelFormatSize(format) / 8;
        for (uint32_t i = 0; i < mipLevels; ++i) {
            size += (uint64_t)w * h * d * bytesPerPixel;
            if (w > 1) w /= 2;
            if (h > 1) h /= 2;
            if (is3D && d > 1) d /= 2;
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
// Instance, validation errors, adapter/device request
// ---------------------------------------------------------------------------

WGPUInstance systems::leal::campello_gpu::getWGPUInstance() {
    static WGPUInstance instance = wgpuCreateInstance(nullptr);
    return instance;
}

// Uncaptured WebGPU errors (validation, out-of-memory, internal) counted for
// validationErrorCount(); see validation_diagnostics.hpp.
static std::atomic<uint64_t> g_validationErrorCount{0};

uint64_t systems::leal::campello_gpu::validationErrorCount() {
    return g_validationErrorCount.load();
}

void systems::leal::campello_gpu::resetValidationErrorCount() {
    g_validationErrorCount.store(0);
}

static void onUncapturedError(WGPUDevice const* device, WGPUErrorType type,
                              WGPUStringView message, void* userdata1, void* userdata2) {
    (void)device; (void)userdata1; (void)userdata2;
    g_validationErrorCount++;
    fprintf(stderr, "[WebGPU ERROR] (type %d) %s\n", static_cast<int>(type),
            fromStringView(message).c_str());
}

static void onDeviceLost(WGPUDevice const* device, WGPUDeviceLostReason reason,
                         WGPUStringView message, void* userdata1, void* userdata2) {
    (void)device; (void)userdata1; (void)userdata2;
    if (reason == WGPUDeviceLostReason_Destroyed) return;  // normal teardown
    fprintf(stderr, "[WebGPU] device lost (reason %d): %s\n", static_cast<int>(reason),
            fromStringView(message).c_str());
}

static WGPUAdapter requestAdapter() {
    WGPUInstance instance = getWGPUInstance();
    if (!instance) return nullptr;

    WGPURequestAdapterOptions options{};
    options.powerPreference = WGPUPowerPreference_HighPerformance;

    WGPUAsyncResult result;
    wgpuInstanceRequestAdapter(instance, &options,
                               makeCallbackInfo<WGPURequestAdapterCallbackInfo>(&result));
    waitForAsync(result);

    if (result.status != static_cast<int>(WGPURequestAdapterStatus_Success) || !result.handle) {
        fprintf(stderr, "[WebGPU] requestAdapter failed: %s\n", result.message.c_str());
        return nullptr;
    }
    return static_cast<WGPUAdapter>(result.handle);
}

static WGPUDevice requestDevice(WGPUAdapter adapter) {
    // Every optional feature the adapter has; requesting more than needed is
    // free, and getFeatures() then reports what was actually enabled.
    std::vector<WGPUFeatureName> features;
    for (WGPUFeatureName f : desiredOptionalFeatures()) {
        if (wgpuAdapterHasFeature(adapter, f)) features.push_back(f);
    }

    // Request the adapter's full limits instead of the spec defaults.
    WGPULimits limits{};
    bool haveLimits = wgpuAdapterGetLimits(adapter, &limits) == WGPUStatus_Success;

    WGPUDeviceDescriptor desc{};
    desc.label = toStringView("campello_gpu device");
    desc.requiredFeatureCount = features.size();
    desc.requiredFeatures = features.data();
    desc.requiredLimits = haveLimits ? &limits : nullptr;
    desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    desc.deviceLostCallbackInfo.callback = onDeviceLost;
    desc.uncapturedErrorCallbackInfo.callback = onUncapturedError;

    WGPUAsyncResult result;
    wgpuAdapterRequestDevice(adapter, &desc,
                             makeCallbackInfo<WGPURequestDeviceCallbackInfo>(&result));
    waitForAsync(result);

    if (result.status != static_cast<int>(WGPURequestDeviceStatus_Success) || !result.handle) {
        fprintf(stderr, "[WebGPU] requestDevice failed: %s\n", result.message.c_str());
        return nullptr;
    }
    return static_cast<WGPUDevice>(result.handle);
}

// Reads the canvas drawing-buffer size (canvas.width/height).
static void getCanvasSize(const std::string& selector, uint32_t& width, uint32_t& height) {
    int w = 0, h = 0;
    if (emscripten_get_canvas_element_size(selector.c_str(), &w, &h) != EMSCRIPTEN_RESULT_SUCCESS) {
        w = h = 0;
    }
    width = static_cast<uint32_t>(std::max(w, 1));
    height = static_cast<uint32_t>(std::max(h, 1));
}

static void configureSurface(WebGPUDeviceData* deviceData, uint32_t width, uint32_t height) {
    WGPUSurfaceConfiguration config{};
    config.device = deviceData->device;
    config.format = deviceData->surfaceFormat;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = width;
    config.height = height;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(deviceData->surface, &config);
    deviceData->surfaceConfigured = true;
    deviceData->surfaceWidth = width;
    deviceData->surfaceHeight = height;
}

static bool createSurface(WebGPUDeviceData* deviceData, const char* canvasSelector) {
    deviceData->canvasSelector = canvasSelector;

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasSource{};
    canvasSource.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasSource.selector = toStringView(deviceData->canvasSelector);

    WGPUSurfaceDescriptor surfaceDesc{};
    surfaceDesc.nextInChain = &canvasSource.chain;
    deviceData->surface = wgpuInstanceCreateSurface(deviceData->instance, &surfaceDesc);
    if (!deviceData->surface) return false;

    // The first reported format is the browser's preferred canvas format
    // (navigator.gpu.getPreferredCanvasFormat()).
    WGPUSurfaceCapabilities caps{};
    if (wgpuSurfaceGetCapabilities(deviceData->surface, deviceData->adapter, &caps) == WGPUStatus_Success &&
        caps.formatCount > 0) {
        deviceData->surfaceFormat = caps.formats[0];
    } else {
        deviceData->surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    uint32_t width = 0, height = 0;
    getCanvasSize(deviceData->canvasSelector, width, height);
    configureSurface(deviceData, width, height);
    return true;
}

// WebGPU reports validation errors asynchronously: creating an invalid
// pipeline returns a non-null *error object* that only fails when used. The
// other backends return nullptr instead, so pipeline creation is wrapped in a
// validation error scope and the result awaited (blocking via ASYNCIFY).
// Errors caught this way are printed but don't reach the uncaptured-error
// callback, so they don't count towards validationErrorCount().
static void pushValidationScope(WGPUDevice device) {
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
}

static bool popValidationScope(WGPUDevice device, const char* what) {
    WGPUAsyncResult result;
    wgpuDevicePopErrorScope(device, makeCallbackInfo<WGPUPopErrorScopeCallbackInfo>(&result));
    waitForAsync(result);
    if (result.status != static_cast<int>(WGPUPopErrorScopeStatus_Success)) {
        return true;  // scope couldn't be read (e.g. device lost); don't guess
    }
    if (result.value == static_cast<int>(WGPUErrorType_NoError)) return true;
    fprintf(stderr, "[WebGPU] %s failed: %s\n", what, result.message.c_str());
    return false;
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

// `pd` is a WGPUDevice obtained by createDevice(); takes ownership of it.
Device::Device(void* pd) {
    auto* deviceData = new WebGPUDeviceData();
    deviceData->instance = getWGPUInstance();
    deviceData->device = static_cast<WGPUDevice>(pd);
    deviceData->queue = wgpuDeviceGetQueue(deviceData->device);
    deviceData->timestampQueryEnabled =
        wgpuDeviceHasFeature(deviceData->device, WGPUFeatureName_TimestampQuery);
    native = deviceData;
    campello_gpu_web_init();
}

Device::~Device() {
    if (native != nullptr) {
        auto* deviceData = static_cast<WebGPUDeviceData*>(native);
        deviceData->alive->store(false);
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
        if (deviceData->surface) {
            if (deviceData->surfaceConfigured) wgpuSurfaceUnconfigure(deviceData->surface);
            wgpuSurfaceRelease(deviceData->surface);
        }
        if (deviceData->queue) {
            wgpuQueueRelease(deviceData->queue);
        }
        if (deviceData->device) {
            wgpuDeviceRelease(deviceData->device);
        }
        if (deviceData->adapter) {
            wgpuAdapterRelease(deviceData->adapter);
        }
        // deviceData->instance is the process-wide instance; not released here.
        delete deviceData;
    }
}

std::string Device::getName() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    std::string name;
    if (deviceData->adapter) {
        WGPUAdapterInfo info{};
        if (wgpuAdapterGetInfo(deviceData->adapter, &info) == WGPUStatus_Success) {
            // Browsers often redact `description`/`device`; fall back to
            // vendor + architecture, which are usually exposed.
            name = fromStringView(info.description);
            if (name.empty()) name = fromStringView(info.device);
            if (name.empty()) {
                name = fromStringView(info.vendor);
                std::string arch = fromStringView(info.architecture);
                if (!arch.empty()) name += (name.empty() ? "" : " ") + arch;
            }
            wgpuAdapterInfoFreeMembers(info);
        }
    }
    return name.empty() ? "WebGPU Device" : name;
}

std::set<Feature> Device::getFeatures() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    WGPUDevice device = deviceData->device;
    return featuresFromWGPU([device](WGPUFeatureName f) {
        return device && wgpuDeviceHasFeature(device, f);
    });
}

std::vector<CooperativeMatrixProperties> Device::getCooperativeMatrixProperties() {
    // Not implemented -- WGSL's cooperative/subgroup-matrix proposal is still
    // in design discussion, unshipped (see featuresFromWGPU() in adapter.cpp
    // and TODO.md). Feature::cooperativeMatrix is never set here either.
    return {};
}

DeviceMemoryInfo Device::getMemoryInfo() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    DeviceMemoryInfo info{};
    // The web exposes no device memory size, budget or working set, so
    // totalDeviceMemory / availableDeviceMemory / recommendedMaxWorkingSet
    // stay 0 (unknown). What is known is what this device allocated.
    info.currentAllocatedSize = deviceData->bufferBytes.load() + deviceData->textureBytes.load() +
                                deviceData->querySetBytes.load();
    // CPU access always goes through copies (mapAsync / queue writes), never
    // shared memory, whatever the GPU's actual architecture.
    info.hasUnifiedMemory = false;
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
    rms.totalTrackedBytes = rms.bufferBytes + rms.textureBytes +
                            rms.accelerationStructureBytes + rms.querySetBytes;
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
    pps.totalPassTimeNs = pps.renderPassTimeNs + pps.computePassTimeNs + pps.rayTracingPassTimeNs;
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

// Builds the TextureHandle for a WGPUTexture (taking over one reference):
// metadata, the default view and the resource/memory bookkeeping. Shared by
// createTexture() and createTextureFromWGPUTexture().
static TextureHandle* makeTextureHandle(WebGPUDeviceData* deviceData, WGPUTexture texture,
                                        TextureType type, PixelFormat pixelFormat,
                                        uint32_t width, uint32_t height, uint32_t depth,
                                        uint32_t mipLevels, uint32_t samples,
                                        TextureUsage usageMode, WGPUTextureFormat format) {
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
    handle->allocatedSize = calculateTextureSize(width, height, depth, mipLevels, pixelFormat,
                                                 type == TextureType::tt3d);
    handle->deviceData = deviceData;

    WGPUTextureViewDescriptor viewDesc{};
    viewDesc.format = format;
    viewDesc.dimension = toWGPUTextureViewDimension(type, (type == TextureType::tt3d) ? 1 : depth);
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = mipLevels;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = (type == TextureType::tt3d) ? 1 : depth;
    viewDesc.aspect = WGPUTextureAspect_All;
    handle->defaultView = wgpuTextureCreateView(texture, &viewDesc);

    deviceData->textureCount++;
    deviceData->textureBytes.fetch_add(handle->allocatedSize);
    updatePeakMemory(deviceData);
    return handle;
}

std::shared_ptr<Texture> Device::createTexture(
    TextureType type, PixelFormat pixelFormat,
    uint32_t width, uint32_t height, uint32_t depth,
    uint32_t mipLevels, uint32_t samples,
    TextureUsage usageMode) {

    auto* deviceData = static_cast<WebGPUDeviceData*>(native);

    // Layer count per type, matching the native backends: a cube always has
    // 6 faces whatever `depth` says, a cube array 6 x N (default one cube).
    uint32_t layers = depth;
    switch (type) {
        case TextureType::tt1d: layers = 1; break;
        case TextureType::ttCube: layers = 6; break;
        case TextureType::ttCubeArray: layers = (depth >= 6) ? depth - depth % 6 : 6; break;
        default: break;
    }
    depth = std::max(layers, 1u);

    WGPUTextureDescriptor desc{};
    desc.dimension = toWGPUTextureDimension(type);
    desc.format = toWGPUTextureFormat(pixelFormat);
    desc.size.width = width;
    desc.size.height = (type == TextureType::tt1d) ? 1 : height;
    desc.size.depthOrArrayLayers = depth;
    desc.mipLevelCount = mipLevels;
    desc.sampleCount = samples;
    desc.usage = toWGPUTextureUsage(usageMode);
    // Texture::upload()/download() go through queue writes and copies, which
    // need CopyDst/CopySrc on WebGPU (other backends need no usage for them).
    // Multisampled textures can't be copied, so they're left as requested.
    if (samples <= 1) {
        desc.usage |= WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
    }

    WGPUTexture texture = wgpuDeviceCreateTexture(deviceData->device, &desc);
    if (!texture) return nullptr;

    TextureHandle* handle = makeTextureHandle(deviceData, texture, type, pixelFormat, width, height,
                                              depth, mipLevels, samples, usageMode, desc.format);
    return std::shared_ptr<Texture>(new Texture(handle));
}

std::shared_ptr<Buffer> Device::createBuffer(uint64_t size, BufferUsage usage) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);

    WGPUBufferDescriptor desc{};
    // WebGPU copies and queue writes work in 4-byte units: allocate a whole
    // number of words so a buffer of any size (e.g. three uint16 indices,
    // 6 bytes) can be written and read in full. getLength() stays `size`.
    desc.size = (size + 3) & ~uint64_t(3);
    desc.usage = toWGPUBufferUsage(usage);
    // Buffer::upload() is a queue write (needs CopyDst) and download() a copy
    // (needs CopySrc), and the native backends can resolve queries into any
    // buffer. Add those usages implicitly, within WebGPU's rule that MapRead
    // may only be combined with CopyDst and MapWrite only with CopySrc.
    if (desc.usage & WGPUBufferUsage_MapRead) {
        desc.usage |= WGPUBufferUsage_CopyDst;
    } else if (desc.usage & WGPUBufferUsage_MapWrite) {
        desc.usage |= WGPUBufferUsage_CopySrc;
    } else {
        desc.usage |= WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc | WGPUBufferUsage_QueryResolve;
    }
    desc.mappedAtCreation = false;

    WGPUBuffer buffer = wgpuDeviceCreateBuffer(deviceData->device, &desc);
    if (!buffer) return nullptr;

    auto* handle = new BufferHandle();
    handle->buffer = buffer;
    handle->usage = desc.usage;
    handle->size = size;
    handle->allocatedSize = desc.size;
    handle->deviceData = deviceData;

    deviceData->bufferCount++;
    deviceData->bufferBytes.fetch_add(desc.size);
    updatePeakMemory(deviceData);

    return std::shared_ptr<Buffer>(new Buffer(handle));
}

// createBuffer(uint64_t, BufferUsage, void*) is implemented in src/pi/utils.cpp

std::shared_ptr<ShaderModule> Device::createShaderModule(const uint8_t* buffer, uint64_t size) {
    // Browsers only accept WGSL (no SPIR-V), so the byte buffer is treated as
    // UTF-8 WGSL source text. This lets callers that load shaders from files
    // use one code path on every backend. Like the other backends, any bytes
    // give a module; invalid source surfaces when a pipeline uses it (empty
    // input is a valid, empty WGSL module).
    std::string source;
    if (buffer && size > 0) {
        source.assign(reinterpret_cast<const char*>(buffer), static_cast<size_t>(size));
    }
    return createShaderModule(source.c_str());
}

std::shared_ptr<ShaderModule> Device::createShaderModule(const char* wgslSource) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);

    WGPUShaderModule shader = createWGSLShaderModule(deviceData->device, wgslSource);
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
    vertexState.entryPoint = toStringView(descriptor.vertex.entryPoint);

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
        depthStencilState->depthWriteEnabled = descriptor.depthStencil->depthWriteEnabled
                                               ? WGPUOptionalBool_True : WGPUOptionalBool_False;
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
    multisampleState.count = std::max(descriptor.sampleCount, 1u);
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
        fragmentState->entryPoint = toStringView(descriptor.fragment->entryPoint);

        colorTargets.reserve(descriptor.fragment->targets.size());
        blendStates.reserve(descriptor.fragment->targets.size());
        for (const auto& target : descriptor.fragment->targets) {
            WGPUColorTargetState ct{};
            ct.format = toWGPUTextureFormat(target.format);
            ct.writeMask = static_cast<WGPUColorWriteMask>(target.writeMask);
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

    pushValidationScope(deviceData->device);
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(deviceData->device, &desc);
    bool valid = popValidationScope(deviceData->device, "createRenderPipeline");
    if (!pipeline) return nullptr;
    if (!valid) {
        wgpuRenderPipelineRelease(pipeline);
        return nullptr;
    }

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

    WGPUPipelineLayout layout = nullptr;
    if (descriptor.layout && descriptor.layout->native) {
        auto* plHandle = static_cast<PipelineLayoutHandle*>(descriptor.layout->native);
        layout = plHandle->layout;
    }

    WGPUComputePipelineDescriptor desc{};
    desc.compute.module = compHandle->shaderModule;
    desc.compute.entryPoint = toStringView(descriptor.compute.entryPoint);
    desc.layout = layout;

    pushValidationScope(deviceData->device);
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(deviceData->device, &desc);
    bool valid = popValidationScope(deviceData->device, "createComputePipeline");
    if (!pipeline) return nullptr;
    if (!valid) {
        wgpuComputePipelineRelease(pipeline);
        return nullptr;
    }

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

std::shared_ptr<BindGroup> Device::createBindGroup(const BindGroupDescriptor& descriptor, bool persistent) {
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
    desc.maxAnisotropy = static_cast<uint16_t>(std::max<double>(descriptor.maxAnisotropy, 1.0));  // 0 is invalid
    // WebGPU only allows anisotropy when min, mag *and* mip filtering are all
    // linear (Metal/Vulkan don't require the mip filter). With linear min/mag,
    // use linear mip filtering so the requested anisotropy still applies;
    // otherwise anisotropy can't be used and is dropped.
    if (desc.maxAnisotropy > 1) {
        if (desc.minFilter == WGPUFilterMode_Linear && desc.magFilter == WGPUFilterMode_Linear) {
            desc.mipmapFilter = WGPUMipmapFilterMode_Linear;
        } else {
            desc.maxAnisotropy = 1;
        }
    }
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
    // Both occlusion and timestamp queries resolve to 8 bytes each.
    deviceData->querySetBytes.fetch_add(static_cast<uint64_t>(descriptor.count) * sizeof(uint64_t));
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
    // GPU timing queries are created lazily by the first pass (see
    // CommandEncoder::beginRenderPass()/beginComputePass()).

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

// Maps a submitted command buffer's resolved pass timestamps (the map
// resolves once the GPU has finished that work), then records the command
// buffer's total GPU time and feeds getPassPerformanceStats().
static void startGpuTimingReadback(const std::shared_ptr<GpuTimingState>& timing) {
    timing->submitted = true;
    std::shared_ptr<GpuTimingState> keep = timing;
    auto* result = newAsyncResult([keep](WGPUAsyncResult& res) {
        GpuTimingState& t = *keep;
        if (res.status == static_cast<int>(WGPUMapAsyncStatus_Success)) {
            const auto* ts = static_cast<const uint64_t*>(
                wgpuBufferGetConstMappedRange(t.readbackBuffer, 0, t.readbackSize));
            if (ts) {
                uint64_t first = UINT64_MAX, last = 0;
                uint64_t renderNs = 0, computeNs = 0;
                uint32_t renderSamples = 0, computeSamples = 0;
                for (const auto& pass : t.passes) {
                    uint64_t begin = ts[pass.beginIndex];
                    uint64_t end = ts[pass.endIndex];
                    if (begin == 0 || end <= begin) continue;  // not written / invalid
                    first = std::min(first, begin);
                    last = std::max(last, end);
                    // WebGPU timestamps are in nanoseconds.
                    if (pass.isCompute) { computeNs += end - begin; computeSamples++; }
                    else { renderNs += end - begin; renderSamples++; }
                }
                if (first != UINT64_MAX && last > first) t.totalNs = last - first;

                if (t.deviceData && t.deviceAlive && t.deviceAlive->load()) {
                    t.deviceData->renderPassTimeNs.fetch_add(renderNs);
                    t.deviceData->computePassTimeNs.fetch_add(computeNs);
                    t.deviceData->renderPassSampleCount.fetch_add(renderSamples);
                    t.deviceData->computePassSampleCount.fetch_add(computeSamples);
                }
            }
            wgpuBufferUnmap(t.readbackBuffer);
        }
        t.done = true;
        auto waiters = std::move(t.waiters);
        t.waiters.clear();
        for (auto& waiter : waiters) waiter(t.totalNs);
    });
    wgpuBufferMapAsync(timing->readbackBuffer, WGPUMapMode_Read, 0,
                       static_cast<size_t>(timing->readbackSize),
                       makeCallbackInfo<WGPUBufferMapCallbackInfo>(result));
}

void Device::submit(std::shared_ptr<CommandBuffer> commandBuffer) {
    if (!commandBuffer || !commandBuffer->native) return;
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    auto* cbHandle = static_cast<CommandBufferHandle*>(commandBuffer->native);

    WGPUCommandBuffer buffers[] = { cbHandle->commandBuffer };
    wgpuQueueSubmit(deviceData->queue, 1, buffers);
    deviceData->commandsSubmitted++;

    // No explicit present: on the web the canvas texture is presented when
    // control returns to the browser at the end of the frame task.

    if (cbHandle->timing && !cbHandle->timing->submitted) {
        startGpuTimingReadback(cbHandle->timing);
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
    // Nothing to schedule: the browser presents the canvas texture
    // automatically at the end of the current frame task.
    (void)deviceData;
}

void Device::waitForIdle() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    WGPUAsyncResult result;
    wgpuQueueOnSubmittedWorkDone(deviceData->queue,
                                 makeCallbackInfo<WGPUQueueWorkDoneCallbackInfo>(&result));
    waitForAsync(result);
}

std::shared_ptr<Fence> Device::createFence() {
    auto* handle = new FenceHandle();
    handle->signaled.store(false);
    return std::shared_ptr<Fence>(new Fence(handle));
}

std::shared_ptr<Device> Device::createDefaultDevice(void* pd) {
    auto adapters = getAdapters();
    if (adapters.empty()) return nullptr;
    return createDevice(adapters[0], pd);
}

// `pd` is an optional CSS selector for the target <canvas> (e.g. "#canvas");
// nullptr creates a headless device (offscreen rendering and compute only).
// Blocks via ASYNCIFY until the browser grants the device.
std::shared_ptr<Device> Device::createDevice(std::shared_ptr<Adapter> adapter, void* pd) {
    auto* adapterHandle = adapter ? static_cast<AdapterHandle*>(adapter->native) : nullptr;
    if (!adapterHandle || !adapterHandle->adapter) return nullptr;

    // WebGPU adapters are single-use: after one requestDevice() the adapter is
    // "consumed". Other backends allow several devices per Adapter, so for
    // every device after the first, request a fresh adapter for the same GPU.
    WGPUAdapter wgpuAdapter = nullptr;
    if (adapterHandle->consumed) {
        wgpuAdapter = requestAdapter();  // owned by the device below
        if (!wgpuAdapter) return nullptr;
    } else {
        wgpuAdapter = adapterHandle->adapter;
        wgpuAdapterAddRef(wgpuAdapter);
    }

    WGPUDevice device = requestDevice(wgpuAdapter);
    adapterHandle->consumed = true;  // even a failed request consumes it
    if (!device) {
        wgpuAdapterRelease(wgpuAdapter);
        return nullptr;
    }

    auto dev = std::shared_ptr<Device>(new Device(device));
    auto* deviceData = static_cast<WebGPUDeviceData*>(dev->native);
    deviceData->adapter = wgpuAdapter;

    if (pd != nullptr && !createSurface(deviceData, static_cast<const char*>(pd))) {
        return nullptr;
    }
    return dev;
}

// The web exposes a single adapter per request (the browser picks the GPU),
// so this returns at most one entry. Blocks via ASYNCIFY.
std::vector<std::shared_ptr<Adapter>> Device::getAdapters() {
    std::vector<std::shared_ptr<Adapter>> adapters;
    WGPUAdapter wgpuAdapter = requestAdapter();
    if (!wgpuAdapter) return adapters;

    auto adapter = std::shared_ptr<Adapter>(new Adapter());
    auto* handle = new AdapterHandle();
    handle->adapter = wgpuAdapter;
    adapter->native = handle;
    adapters.push_back(adapter);
    return adapters;
}

void* Device::getNativeWGPUDevice() {
    return static_cast<WebGPUDeviceData*>(native)->device;
}

std::shared_ptr<Texture> Device::createTextureFromWGPUTexture(void* wgpuTexture) {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    auto texture = static_cast<WGPUTexture>(wgpuTexture);
    if (!texture) return nullptr;

    // Read the texture's shape from WebGPU itself.
    uint32_t width = wgpuTextureGetWidth(texture);
    uint32_t height = wgpuTextureGetHeight(texture);
    uint32_t depth = wgpuTextureGetDepthOrArrayLayers(texture);
    uint32_t mipLevels = wgpuTextureGetMipLevelCount(texture);
    uint32_t samples = wgpuTextureGetSampleCount(texture);
    WGPUTextureFormat format = wgpuTextureGetFormat(texture);

    // A 2D texture with 6 layers can't be told apart from a cube here; it
    // stays tt2d (a 2D array), which is what its default view will be.
    TextureType type = TextureType::tt2d;
    switch (wgpuTextureGetDimension(texture)) {
        case WGPUTextureDimension_1D: type = TextureType::tt1d; break;
        case WGPUTextureDimension_3D: type = TextureType::tt3d; break;
        default: break;
    }

    wgpuTextureAddRef(texture);  // the caller keeps their own reference
    TextureHandle* handle = makeTextureHandle(
        deviceData, texture, type, wgpuFormatToPixelFormat(format), width, height, depth,
        mipLevels, samples, fromWGPUTextureUsage(wgpuTextureGetUsage(texture)), format);
    return std::shared_ptr<Texture>(new Texture(handle));
}

std::string Device::getEngineVersion() {
    return std::to_string(campello_gpu_VERSION_MAJOR) + "." +
           std::to_string(campello_gpu_VERSION_MINOR) + "." +
           std::to_string(campello_gpu_VERSION_PATCH);
}

std::shared_ptr<TextureView> Device::getSwapchainTextureView() {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    if (!deviceData->surface) return nullptr;

    // Follow canvas resizes: reconfigure when the drawing-buffer size changed.
    uint32_t width = 0, height = 0;
    getCanvasSize(deviceData->canvasSelector, width, height);
    if (!deviceData->surfaceConfigured ||
        width != deviceData->surfaceWidth || height != deviceData->surfaceHeight) {
        configureSurface(deviceData, width, height);
    }

    WGPUSurfaceTexture surfaceTexture{};
    wgpuSurfaceGetCurrentTexture(deviceData->surface, &surfaceTexture);
    if (!surfaceTexture.texture) return nullptr;

    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, nullptr);
    wgpuTextureRelease(surfaceTexture.texture);  // the view keeps it alive
    if (!view) return nullptr;

    auto* handle = new TextureViewHandle();
    handle->view = view;
    handle->ownsView = true;
    handle->format = deviceData->surfaceFormat;
    return std::shared_ptr<TextureView>(new TextureView(handle));
}

PixelFormat Device::getSwapchainPixelFormat() const noexcept {
    auto* deviceData = static_cast<WebGPUDeviceData*>(native);
    if (!deviceData->surface) return PixelFormat::invalid;
    return wgpuFormatToPixelFormat(deviceData->surfaceFormat);
}

std::string systems::leal::campello_gpu::getVersion() {
    return Device::getEngineVersion();
}
