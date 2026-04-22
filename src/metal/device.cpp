#include "Metal.hpp"
#include "common.hpp"
#include "render_pipeline_handle.hpp"
#include "shader_module_handle.hpp"
#include "acceleration_structure_handle.hpp"
#include "ray_tracing_pipeline_handle.hpp"
#include "acceleration_structure_helpers.hpp"
#include "bind_group_data.hpp"
#include "bind_group_layout_data.hpp"
#include "buffer_handle.hpp"
#include "texture_handle.hpp"
#include "campello_gpu_config.h"
#include "TargetConditionals.h"
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
#include <dispatch/dispatch.h>
#include <iostream>
#include <cmath>
#include <atomic>
#include <unistd.h>

using namespace systems::leal::campello_gpu;

// --- Mapping helpers ---

static MTL::SamplerAddressMode toMTLAddressMode(WrapMode mode) {
    switch (mode) {
        case WrapMode::repeat:       return MTL::SamplerAddressModeRepeat;
        case WrapMode::mirrorRepeat: return MTL::SamplerAddressModeMirrorRepeat;
        default:                     return MTL::SamplerAddressModeClampToEdge;
    }
}

static MTL::SamplerMinMagFilter toMTLFilter(FilterMode mode) {
    switch (mode) {
        case FilterMode::fmLinear:
        case FilterMode::fmLinearMipmapNearest:
        case FilterMode::fmLinearMipmapLinear:  return MTL::SamplerMinMagFilterLinear;
        default:                                return MTL::SamplerMinMagFilterNearest;
    }
}

static MTL::SamplerMipFilter toMTLMipFilter(FilterMode mode) {
    switch (mode) {
        case FilterMode::fmNearestMipmapNearest:
        case FilterMode::fmLinearMipmapNearest:  return MTL::SamplerMipFilterNearest;
        case FilterMode::fmNearestMipmapLinear:
        case FilterMode::fmLinearMipmapLinear:   return MTL::SamplerMipFilterLinear;
        default:                                 return MTL::SamplerMipFilterNotMipmapped;
    }
}

static MTL::CompareFunction toMTLCompare(CompareOp op) {
    switch (op) {
        case CompareOp::never:        return MTL::CompareFunctionNever;
        case CompareOp::less:         return MTL::CompareFunctionLess;
        case CompareOp::equal:        return MTL::CompareFunctionEqual;
        case CompareOp::lessEqual:    return MTL::CompareFunctionLessEqual;
        case CompareOp::greater:      return MTL::CompareFunctionGreater;
        case CompareOp::notEqual:     return MTL::CompareFunctionNotEqual;
        case CompareOp::greaterEqual: return MTL::CompareFunctionGreaterEqual;
        default:                      return MTL::CompareFunctionAlways;
    }
}

static MTL::VertexFormat toMTLVertexFormat(ComponentType comp, AccessorType acc, bool normalized) {
    switch (acc) {
        case AccessorType::acScalar:
            switch (comp) {
                case ComponentType::ctFloat:         return MTL::VertexFormatFloat;
                case ComponentType::ctUnsignedInt:   return MTL::VertexFormatUInt;
                case ComponentType::ctByte:          return normalized ? MTL::VertexFormatCharNormalized : MTL::VertexFormatChar;
                case ComponentType::ctUnsignedByte:  return normalized ? MTL::VertexFormatUCharNormalized : MTL::VertexFormatUChar;
                case ComponentType::ctShort:         return normalized ? MTL::VertexFormatShortNormalized : MTL::VertexFormatShort;
                case ComponentType::ctUnsignedShort: return normalized ? MTL::VertexFormatUShortNormalized : MTL::VertexFormatUShort;
                default:                             return MTL::VertexFormatFloat;
            }
        case AccessorType::acVec2:
            switch (comp) {
                case ComponentType::ctFloat:         return MTL::VertexFormatFloat2;
                case ComponentType::ctUnsignedInt:   return MTL::VertexFormatUInt2;
                case ComponentType::ctByte:          return normalized ? MTL::VertexFormatChar2Normalized : MTL::VertexFormatChar2;
                case ComponentType::ctUnsignedByte:  return normalized ? MTL::VertexFormatUChar2Normalized : MTL::VertexFormatUChar2;
                case ComponentType::ctShort:         return normalized ? MTL::VertexFormatShort2Normalized : MTL::VertexFormatShort2;
                case ComponentType::ctUnsignedShort: return normalized ? MTL::VertexFormatUShort2Normalized : MTL::VertexFormatUShort2;
                default:                             return MTL::VertexFormatFloat2;
            }
        case AccessorType::acVec3:
            switch (comp) {
                case ComponentType::ctFloat:         return MTL::VertexFormatFloat3;
                case ComponentType::ctUnsignedInt:   return MTL::VertexFormatUInt3;
                case ComponentType::ctByte:          return normalized ? MTL::VertexFormatChar3Normalized : MTL::VertexFormatChar3;
                case ComponentType::ctUnsignedByte:  return normalized ? MTL::VertexFormatUChar3Normalized : MTL::VertexFormatUChar3;
                case ComponentType::ctShort:         return normalized ? MTL::VertexFormatShort3Normalized : MTL::VertexFormatShort3;
                case ComponentType::ctUnsignedShort: return normalized ? MTL::VertexFormatUShort3Normalized : MTL::VertexFormatUShort3;
                default:                             return MTL::VertexFormatFloat3;
            }
        case AccessorType::acVec4:
            switch (comp) {
                case ComponentType::ctFloat:         return MTL::VertexFormatFloat4;
                case ComponentType::ctUnsignedInt:   return MTL::VertexFormatUInt4;
                case ComponentType::ctByte:          return normalized ? MTL::VertexFormatChar4Normalized : MTL::VertexFormatChar4;
                case ComponentType::ctUnsignedByte:  return normalized ? MTL::VertexFormatUChar4Normalized : MTL::VertexFormatUChar4;
                case ComponentType::ctShort:         return normalized ? MTL::VertexFormatShort4Normalized : MTL::VertexFormatShort4;
                case ComponentType::ctUnsignedShort: return normalized ? MTL::VertexFormatUShort4Normalized : MTL::VertexFormatUShort4;
                default:                             return MTL::VertexFormatFloat4;
            }
        default:
            return MTL::VertexFormatFloat;
    }
}

// --- Device ---

Device::Device(void *pd) {
    auto *deviceData = new MetalDeviceData();
    deviceData->device       = static_cast<MTL::Device *>(pd);
    deviceData->commandQueue = deviceData->device->newCommandQueue();
    native = deviceData;

    auto *dev = deviceData->device;
    std::cout << "  - name: " << getName() << std::endl;
    std::cout << "  - supportsRayTracing: " << dev->supportsRaytracing() << std::endl;
    std::cout << "  - supports32BitMSAA: " << dev->supports32BitMSAA() << std::endl;
    std::cout << "  - supportsBCTextureCompression: " << dev->supportsBCTextureCompression() << std::endl;
    std::cout << "  - hasUnifiedMemory: " << dev->hasUnifiedMemory() << std::endl;
    std::cout << "  - currentAllocatedSize: " << dev->currentAllocatedSize() / (1024 * 1024.0) << "Mb." << std::endl;
    std::cout << "  - recommendedMaxWorkingSetSize: " << dev->recommendedMaxWorkingSetSize() / (1024 * 1024.0) << "Mb." << std::endl;
    
    // Calibrate GPU timestamps using sampleTimestamps
    MTL::Timestamp cpuTimestamp1, gpuTimestamp1;
    MTL::Timestamp cpuTimestamp2, gpuTimestamp2;
    dev->sampleTimestamps(&cpuTimestamp1, &gpuTimestamp1);
    // Small delay to get a measurable delta
    usleep(1000);  // 1ms
    dev->sampleTimestamps(&cpuTimestamp2, &gpuTimestamp2);
    
    // Calculate GPU clock to nanoseconds conversion
    uint64_t cpuDeltaNs = (cpuTimestamp2 - cpuTimestamp1);  // CPU time is in nanoseconds
    uint64_t gpuDeltaTicks = gpuTimestamp2 - gpuTimestamp1;
    
    if (gpuDeltaTicks > 0) {
        deviceData->gpuTimestampNumerator = cpuDeltaNs;
        deviceData->gpuTimestampDenominator = gpuDeltaTicks;
    }
    
    // Create timestamp query buffer (64 timestamps, 8 bytes each)
    deviceData->timestampQueryBuffer = dev->newBuffer(
        64 * sizeof(uint64_t), MTL::ResourceStorageModeShared);
}

Device::~Device() {
    if (native != nullptr) {
        auto *deviceData = static_cast<MetalDeviceData *>(native);
        if (deviceData->timestampQueryBuffer) deviceData->timestampQueryBuffer->release();
        if (deviceData->commandQueue) deviceData->commandQueue->release();
        if (deviceData->device)       deviceData->device->release();
        delete deviceData;
        std::cout << "Device::~Device()" << std::endl;
    }
}

std::shared_ptr<Device> Device::createDefaultDevice(void *pd) {
    auto *device = MTL::CreateSystemDefaultDevice();
    if (device == nullptr) return nullptr;
    std::cout << "Device::createDefaultDevice()" << std::endl;
    return std::shared_ptr<Device>(new Device(device));
}

std::shared_ptr<Device> Device::createDevice(std::shared_ptr<Adapter> adapter, void *pd) {
    if (adapter == nullptr || adapter->native == nullptr) return nullptr;
    auto *mtlDevice = static_cast<MTL::Device *>(adapter->native);
    mtlDevice->retain();
    return std::shared_ptr<Device>(new Device(mtlDevice));
}

std::vector<std::shared_ptr<Adapter>> Device::getAdapters() {
    std::vector<std::shared_ptr<Adapter>> toReturn;
    auto *devices = MTL::CopyAllDevices();
    for (NS::UInteger i = 0; i < devices->count(); ++i) {
        auto *mtlDevice = static_cast<MTL::Device *>(devices->object(i));
        Adapter *adapter = new Adapter();
        adapter->native = mtlDevice;
        toReturn.push_back(std::shared_ptr<Adapter>(adapter));
    }
    devices->release();
    return toReturn;
}

std::string Device::getName() {
    return static_cast<MetalDeviceData *>(native)->device->name()->utf8String();
}

std::set<Feature> Device::getFeatures() {
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    std::set<Feature> toReturn;
    if (dev->supportsRaytracing())
        toReturn.insert(Feature::raytracing);
    if (dev->supports32BitMSAA())
        toReturn.insert(Feature::msaa32bit);
    if (dev->supportsBCTextureCompression())
        toReturn.insert(Feature::bcTextureCompression);
    if (__builtin_available(macOS 10.11, *)) {
        if (dev->depth24Stencil8PixelFormatSupported())
            toReturn.insert(Feature::depth24Stencil8PixelFormat);
    }
    return toReturn;
}

std::string Device::getEngineVersion() {
    MTL::Device *d = MTL::CreateSystemDefaultDevice();
    if (!d) return "Metal";
    std::string result = "Metal";
#if defined(MTL_GPU_FAMILY_METAL3) || defined(__MAC_13_0) || defined(__IPHONE_16_0)
    if (d->supportsFamily(MTL::GPUFamilyMetal3)) result = "Metal 3";
    else
#endif
    if (d->supportsFamily(MTL::GPUFamilyMac2))   result = "Metal 2";
    else                                          result = "Metal 1";
    d->release();
    return result;
}

// ---------------------------------------------------------------------------
// Metrics and monitoring (Phase 1)
// ---------------------------------------------------------------------------

DeviceMemoryInfo Device::getMemoryInfo() {
    DeviceMemoryInfo info;
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    
    info.currentAllocatedSize = dev->currentAllocatedSize();
    info.recommendedMaxWorkingSet = dev->recommendedMaxWorkingSetSize();
    info.hasUnifiedMemory = dev->hasUnifiedMemory();
    
    // For total memory, we use recommended working set as a proxy
    // on Apple Silicon this is meaningful, on discrete GPUs it's VRAM
    info.totalDeviceMemory = info.hasUnifiedMemory ? 0 : info.recommendedMaxWorkingSet;
    
    // Calculate available as working set minus current (clamped to 0)
    if (info.recommendedMaxWorkingSet > info.currentAllocatedSize) {
        info.availableDeviceMemory = info.recommendedMaxWorkingSet - info.currentAllocatedSize;
    } else {
        info.availableDeviceMemory = 0;
    }
    
    return info;
}

ResourceCounters Device::getResourceCounters() {
    ResourceCounters counters;
    auto *data = static_cast<MetalDeviceData *>(native);
    
    counters.bufferCount = data->bufferCount.load();
    counters.textureCount = data->textureCount.load();
    counters.renderPipelineCount = data->renderPipelineCount.load();
    counters.computePipelineCount = data->computePipelineCount.load();
    counters.rayTracingPipelineCount = data->rayTracingPipelineCount.load();
    counters.accelerationStructureCount = data->accelerationStructureCount.load();
    counters.shaderModuleCount = data->shaderModuleCount.load();
    counters.samplerCount = data->samplerCount.load();
    counters.bindGroupCount = data->bindGroupCount.load();
    counters.bindGroupLayoutCount = data->bindGroupLayoutCount.load();
    counters.pipelineLayoutCount = data->pipelineLayoutCount.load();
    counters.querySetCount = data->querySetCount.load();
    
    return counters;
}

CommandStats Device::getCommandStats() {
    CommandStats stats;
    auto *data = static_cast<MetalDeviceData *>(native);
    
    stats.commandsSubmitted = data->commandsSubmitted.load();
    stats.renderPasses = data->renderPasses.load();
    stats.computePasses = data->computePasses.load();
    stats.rayTracingPasses = data->rayTracingPasses.load();
    stats.drawCalls = data->drawCalls.load();
    stats.dispatchCalls = data->dispatchCalls.load();
    stats.traceRaysCalls = data->traceRaysCalls.load();
    stats.copies = data->copies.load();
    
    return stats;
}

Metrics Device::getMetrics() {
    Metrics m;
    m.deviceMemory = getMemoryInfo();
    m.resources = getResourceCounters();
    m.commands = getCommandStats();
    m.resourceMemory = getResourceMemoryStats();
    return m;
}

void Device::resetCommandStats() {
    auto *data = static_cast<MetalDeviceData *>(native);
    
    data->commandsSubmitted = 0;
    data->renderPasses = 0;
    data->computePasses = 0;
    data->rayTracingPasses = 0;
    data->drawCalls = 0;
    data->dispatchCalls = 0;
    data->traceRaysCalls = 0;
    data->copies = 0;
}

ResourceMemoryStats Device::getResourceMemoryStats() {
    ResourceMemoryStats stats;
    auto *data = static_cast<MetalDeviceData *>(native);
    
    stats.bufferBytes = data->bufferBytes.load();
    stats.textureBytes = data->textureBytes.load();
    stats.accelerationStructureBytes = data->accelerationStructureBytes.load();
    stats.shaderModuleBytes = data->shaderModuleBytes.load();
    stats.querySetBytes = data->querySetBytes.load();
    stats.totalTrackedBytes = stats.bufferBytes + stats.textureBytes + 
                               stats.accelerationStructureBytes + stats.querySetBytes;
    
    stats.peakBufferBytes = data->peakBufferBytes.load();
    stats.peakTextureBytes = data->peakTextureBytes.load();
    stats.peakAccelerationStructureBytes = data->peakAccelerationStructureBytes.load();
    stats.peakTotalBytes = data->peakTotalBytes.load();
    
    return stats;
}

void Device::resetPeakMemoryStats() {
    auto *data = static_cast<MetalDeviceData *>(native);
    
    // Reset peaks to current values
    uint64_t currentBuffer = data->bufferBytes.load();
    uint64_t currentTexture = data->textureBytes.load();
    uint64_t currentAS = data->accelerationStructureBytes.load();
    uint64_t currentTotal = currentBuffer + currentTexture + currentAS + data->querySetBytes.load();
    
    data->peakBufferBytes = currentBuffer;
    data->peakTextureBytes = currentTexture;
    data->peakAccelerationStructureBytes = currentAS;
    data->peakTotalBytes = currentTotal;
}

// ---------------------------------------------------------------------------
// Phase 3: GPU Timing and Memory Pressure
// ---------------------------------------------------------------------------

PassPerformanceStats Device::getPassPerformanceStats() {
    PassPerformanceStats stats;
    auto *data = static_cast<MetalDeviceData *>(native);
    
    stats.renderPassTimeNs = data->renderPassTimeNs.load();
    stats.computePassTimeNs = data->computePassTimeNs.load();
    stats.rayTracingPassTimeNs = data->rayTracingPassTimeNs.load();
    stats.totalPassTimeNs = stats.renderPassTimeNs + stats.computePassTimeNs + stats.rayTracingPassTimeNs;
    stats.renderPassSampleCount = data->renderPassSampleCount.load();
    stats.computePassSampleCount = data->computePassSampleCount.load();
    stats.rayTracingPassSampleCount = data->rayTracingPassSampleCount.load();
    
    return stats;
}

void Device::resetPassPerformanceStats() {
    auto *data = static_cast<MetalDeviceData *>(native);
    
    data->renderPassTimeNs = 0;
    data->computePassTimeNs = 0;
    data->rayTracingPassTimeNs = 0;
    data->renderPassSampleCount = 0;
    data->computePassSampleCount = 0;
    data->rayTracingPassSampleCount = 0;
}

MemoryPressureLevel Device::getMemoryPressureLevel() {
    auto *data = static_cast<MetalDeviceData *>(native);
    auto stats = getResourceMemoryStats();
    
    // Get the recommended working set size
    uint64_t recommendedSize = data->device->recommendedMaxWorkingSetSize();
    if (recommendedSize == 0) {
        // Fallback: use total device memory if available
        recommendedSize = getMemoryInfo().totalDeviceMemory;
    }
    if (recommendedSize == 0) {
        // Can't determine pressure without a budget
        return MemoryPressureLevel::Normal;
    }
    
    uint64_t currentUsage = stats.totalTrackedBytes;
    uint64_t warningThreshold = (recommendedSize * data->memoryBudget.warningThresholdPercent) / 100;
    uint64_t criticalThreshold = (recommendedSize * data->memoryBudget.criticalThresholdPercent) / 100;
    
    if (currentUsage >= criticalThreshold) {
        return MemoryPressureLevel::Critical;
    } else if (currentUsage >= warningThreshold) {
        return MemoryPressureLevel::Warning;
    }
    return MemoryPressureLevel::Normal;
}

void Device::setMemoryBudget(const MemoryBudget& budget) {
    auto *data = static_cast<MetalDeviceData *>(native);
    data->memoryBudget = budget;
}

MemoryBudget Device::getMemoryBudget() {
    auto *data = static_cast<MetalDeviceData *>(native);
    return data->memoryBudget;
}

void Device::setMemoryPressureCallback(MemoryPressureCallback callback) {
    auto *data = static_cast<MetalDeviceData *>(native);
    data->memoryPressureCallback = callback;
}

MemoryPressureLevel Device::checkMemoryPressure() {
    auto *data = static_cast<MetalDeviceData *>(native);
    auto currentLevel = getMemoryPressureLevel();
    auto previousLevel = data->lastPressureLevel.exchange(currentLevel);
    
    // Invoke callback if level changed or if critically pressured
    if (data->memoryPressureCallback && 
        (currentLevel != previousLevel || currentLevel == MemoryPressureLevel::Critical)) {
        data->memoryPressureCallback(currentLevel, getResourceMemoryStats());
    }
    
    // TODO: Implement automatic resource eviction if enabled and critical
    
    return currentLevel;
}

MetricsWithTiming Device::getMetricsWithTiming() {
    MetricsWithTiming m;
    m.deviceMemory = getMemoryInfo();
    m.resources = getResourceCounters();
    m.commands = getCommandStats();
    m.resourceMemory = getResourceMemoryStats();
    m.passPerformance = getPassPerformanceStats();
    return m;
}

// ---------------------------------------------------------------------------
// Resource creation
// ---------------------------------------------------------------------------

std::shared_ptr<Texture> Device::createTexture(
    TextureType type, PixelFormat pixelFormat,
    uint32_t width, uint32_t height, uint32_t depth,
    uint32_t mipLevels, uint32_t samples, TextureUsage usageMode) {

    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    auto *pTextureDesc = MTL::TextureDescriptor::alloc()->init();
    pTextureDesc->setWidth(width);
    pTextureDesc->setHeight(height);
    pTextureDesc->setDepth(depth > 0 ? depth : 1);
    pTextureDesc->setMipmapLevelCount(mipLevels > 0 ? mipLevels : 1);
    pTextureDesc->setSampleCount(samples > 0 ? samples : 1);
    pTextureDesc->setPixelFormat((MTL::PixelFormat)pixelFormat);

    // Depth/stencil textures must use StorageModePrivate on macOS; Managed is invalid.
    bool isDepthStencil = (pixelFormat == PixelFormat::depth16unorm         ||
                           pixelFormat == PixelFormat::depth32float          ||
                           pixelFormat == PixelFormat::depth24plus_stencil8  ||
                           pixelFormat == PixelFormat::depth32float_stencil8 ||
                           pixelFormat == PixelFormat::stencil8);
    MTL::StorageMode colorStorageMode;
#if TARGET_OS_IOS || TARGET_OS_SIMULATOR
    // MTLStorageModeManaged is macOS-only; use Shared on iOS/Simulator.
    colorStorageMode = MTL::StorageModeShared;
#else
    colorStorageMode = MTL::StorageModeManaged;
#endif
    pTextureDesc->setStorageMode(isDepthStencil ? MTL::StorageModePrivate
                                                : colorStorageMode);

    switch (type) {
        case TextureType::tt1d:
            pTextureDesc->setTextureType(MTL::TextureType1D);
            pTextureDesc->setDepth(1);
            pTextureDesc->setArrayLength(1);
            break;
        case TextureType::tt3d:
            pTextureDesc->setTextureType(MTL::TextureType3D);
            pTextureDesc->setArrayLength(1);
            break;
        case TextureType::ttCube:
            pTextureDesc->setTextureType(MTL::TextureTypeCube);
            pTextureDesc->setHeight(width);
            pTextureDesc->setDepth(1);
            pTextureDesc->setArrayLength(1);
            break;
        case TextureType::ttCubeArray:
            pTextureDesc->setTextureType(MTL::TextureTypeCubeArray);
            pTextureDesc->setHeight(width);
            pTextureDesc->setDepth(1);
            pTextureDesc->setArrayLength((depth > 0 ? depth : 6) / 6);
            break;
        default: {
            uint32_t arrLen = depth > 0 ? depth : 1;
            if (arrLen > 1) {
                pTextureDesc->setTextureType(MTL::TextureType2DArray);
            } else {
                pTextureDesc->setTextureType(
                    samples > 1 ? MTL::TextureType2DMultisample : MTL::TextureType2D);
            }
            pTextureDesc->setDepth(1);
            pTextureDesc->setArrayLength(arrLen);
            break;
        }
    }

    MTL::TextureUsage mtlUsage = MTL::TextureUsageUnknown;
    const int u = static_cast<int>(usageMode);
    if (u & static_cast<int>(TextureUsage::textureBinding))
        mtlUsage |= MTL::TextureUsageShaderRead;
    if (u & static_cast<int>(TextureUsage::storageBinding))
        mtlUsage |= MTL::TextureUsageShaderWrite;
    if (u & static_cast<int>(TextureUsage::renderTarget))
        mtlUsage |= MTL::TextureUsageRenderTarget;
    pTextureDesc->setUsage(mtlUsage);

    MTL::Texture *pTexture = dev->newTexture(pTextureDesc);
    pTextureDesc->release();
    if (pTexture == nullptr) return nullptr;
    
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    deviceData->textureCount++;
    
    // Phase 2: Track texture memory
    uint64_t allocatedSize = pTexture->allocatedSize();
    uint64_t newTextureBytes = deviceData->textureBytes.fetch_add(allocatedSize) + allocatedSize;
    
    // Update peak if needed
    uint64_t currentPeak = deviceData->peakTextureBytes.load();
    while (newTextureBytes > currentPeak && !deviceData->peakTextureBytes.compare_exchange_weak(currentPeak, newTextureBytes)) {
        // Retry if another thread updated the peak
    }
    
    // Update total peak
    uint64_t totalBytes = deviceData->bufferBytes.load() + deviceData->textureBytes.load() + 
                          deviceData->accelerationStructureBytes.load() + deviceData->querySetBytes.load();
    uint64_t currentTotalPeak = deviceData->peakTotalBytes.load();
    while (totalBytes > currentTotalPeak && !deviceData->peakTotalBytes.compare_exchange_weak(currentTotalPeak, totalBytes)) {
        // Retry if another thread updated the peak
    }
    
    auto handle = new MetalTextureHandle{pTexture, allocatedSize, deviceData};
    return std::shared_ptr<Texture>(new Texture(handle));
}

std::shared_ptr<Buffer> Device::createBuffer(uint64_t size, BufferUsage usage) {
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    MTL::ResourceOptions options;
    const int u = static_cast<int>(usage);
    if (u & (static_cast<int>(BufferUsage::mapRead) | static_cast<int>(BufferUsage::mapWrite))) {
        options = MTL::ResourceStorageModeShared;
    } else {
#if TARGET_OS_IOS || TARGET_OS_SIMULATOR
        // MTLResourceStorageModeManaged is macOS-only; use Shared on iOS/Simulator.
        options = MTL::ResourceStorageModeShared;
#else
        options = MTL::ResourceStorageModeManaged;
#endif
    }
    MTL::Buffer *pBuffer = dev->newBuffer(size, options);
    if (pBuffer == nullptr) return nullptr;
    
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    deviceData->bufferCount++;
    
    // Phase 2: Track buffer memory
    // Metal buffers are rounded up to page boundaries, use allocatedSize for accurate tracking
    uint64_t allocatedSize = pBuffer->allocatedSize();
    uint64_t newBufferBytes = deviceData->bufferBytes.fetch_add(allocatedSize) + allocatedSize;
    
    // Update peak if needed
    uint64_t currentPeak = deviceData->peakBufferBytes.load();
    while (newBufferBytes > currentPeak && !deviceData->peakBufferBytes.compare_exchange_weak(currentPeak, newBufferBytes)) {
        // Retry if another thread updated the peak
    }
    
    // Update total peak
    uint64_t totalBytes = deviceData->bufferBytes.load() + deviceData->textureBytes.load() + 
                          deviceData->accelerationStructureBytes.load() + deviceData->querySetBytes.load();
    uint64_t currentTotalPeak = deviceData->peakTotalBytes.load();
    while (totalBytes > currentTotalPeak && !deviceData->peakTotalBytes.compare_exchange_weak(currentTotalPeak, totalBytes)) {
        // Retry if another thread updated the peak
    }
    
    auto handle = new MetalBufferHandle{pBuffer, allocatedSize, deviceData};
    return std::shared_ptr<Buffer>(new Buffer(handle));
}

std::shared_ptr<ShaderModule> Device::createShaderModule(const uint8_t *buffer, uint64_t size) {
    auto *data = new MetalShaderModuleData{ std::vector<uint8_t>(buffer, buffer + size) };
    
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    deviceData->shaderModuleCount++;
    
    return std::shared_ptr<ShaderModule>(new ShaderModule(data));
}

std::shared_ptr<RenderPipeline> Device::createRenderPipeline(const RenderPipelineDescriptor &descriptor) {
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    auto *pipelineDesc = MTL::RenderPipelineDescriptor::alloc()->init();

    auto compileModule = [&](ShaderModule *module) -> MTL::Library * {
        auto *data = static_cast<MetalShaderModuleData *>(module->native);
        dispatch_data_t dispData = dispatch_data_create(
            data->bytes.data(), data->bytes.size(), nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        NS::Error *err    = nullptr;
        auto      *lib    = dev->newLibrary(dispData, &err);
        dispatch_release(dispData);
        if (!lib && err)
            std::cerr << "createRenderPipeline: " << err->localizedDescription()->utf8String() << std::endl;
        return lib;
    };

    // A vertex shader is required; return nullptr if missing.
    if (!descriptor.vertex.module) { pipelineDesc->release(); return nullptr; }

    // Vertex function
    if (descriptor.vertex.module) {
        auto *library = compileModule(descriptor.vertex.module.get());
        if (!library) { pipelineDesc->release(); return nullptr; }
        auto *funcName = NS::String::string(descriptor.vertex.entryPoint.c_str(), NS::UTF8StringEncoding);
        auto *fn       = library->newFunction(funcName);
        pipelineDesc->setVertexFunction(fn);
        if (fn) fn->release();
        library->release();
    }

    // Fragment function + color attachments
    if (descriptor.fragment && descriptor.fragment->module) {
        auto *library = compileModule(descriptor.fragment->module.get());
        if (!library) { pipelineDesc->release(); return nullptr; }
        auto *funcName = NS::String::string(descriptor.fragment->entryPoint.c_str(), NS::UTF8StringEncoding);
        auto *fn       = library->newFunction(funcName);
        pipelineDesc->setFragmentFunction(fn);
        if (fn) fn->release();
        library->release();

        for (size_t i = 0; i < descriptor.fragment->targets.size(); i++) {
            const auto& cs = descriptor.fragment->targets[i];
            auto* ca = pipelineDesc->colorAttachments()->object(i);
            ca->setPixelFormat((MTL::PixelFormat)cs.format);
            if (cs.blend) {
                ca->setBlendingEnabled(true);
                ca->setRgbBlendOperation(          (MTL::BlendOperation)cs.blend->color.operation);
                ca->setSourceRGBBlendFactor(       (MTL::BlendFactor)   cs.blend->color.srcFactor);
                ca->setDestinationRGBBlendFactor(  (MTL::BlendFactor)   cs.blend->color.dstFactor);
                ca->setAlphaBlendOperation(        (MTL::BlendOperation)cs.blend->alpha.operation);
                ca->setSourceAlphaBlendFactor(     (MTL::BlendFactor)   cs.blend->alpha.srcFactor);
                ca->setDestinationAlphaBlendFactor((MTL::BlendFactor)   cs.blend->alpha.dstFactor);
            }
        }
    }

    // Depth attachment pixel format
    if (descriptor.depthStencil) {
        pipelineDesc->setDepthAttachmentPixelFormat(
            (MTL::PixelFormat)descriptor.depthStencil->format);
    }

    // Vertex descriptor
    if (!descriptor.vertex.buffers.empty()) {
        auto *vertexDesc = MTL::VertexDescriptor::alloc()->init();
        for (size_t bufIdx = 0; bufIdx < descriptor.vertex.buffers.size(); bufIdx++) {
            const auto &layout = descriptor.vertex.buffers[bufIdx];
            vertexDesc->layouts()->object(bufIdx)->setStride((NS::UInteger)layout.arrayStride);
            vertexDesc->layouts()->object(bufIdx)->setStepFunction(
                layout.stepMode == StepMode::instance
                    ? MTL::VertexStepFunctionPerInstance
                    : MTL::VertexStepFunctionPerVertex);
            for (const auto &attr : layout.attributes) {
                auto *attrDesc = vertexDesc->attributes()->object(attr.shaderLocation);
                attrDesc->setBufferIndex(bufIdx);
                attrDesc->setOffset(attr.offset);
                attrDesc->setFormat(toMTLVertexFormat(attr.componentType, attr.accessorType, attr.normalized));
            }
        }
        pipelineDesc->setVertexDescriptor(vertexDesc);
        vertexDesc->release();
    }

    NS::Error *error       = nullptr;
    auto *pipelineState    = dev->newRenderPipelineState(pipelineDesc, &error);
    pipelineDesc->release();
    if (!pipelineState) {
        if (error) std::cerr << "createRenderPipeline: "
                             << error->localizedDescription()->utf8String() << std::endl;
        return nullptr;
    }

    // Create a MTLDepthStencilState when depth testing is requested.
    // This is a separate object from MTLRenderPipelineState and must be set
    // on the encoder via setDepthStencilState: for depth testing to work.
    MTL::DepthStencilState *depthStencilState = nullptr;
    if (descriptor.depthStencil) {
        auto *dsDesc = MTL::DepthStencilDescriptor::alloc()->init();
        dsDesc->setDepthWriteEnabled(descriptor.depthStencil->depthWriteEnabled);
        dsDesc->setDepthCompareFunction(toMTLCompare(descriptor.depthStencil->depthCompare));
        depthStencilState = dev->newDepthStencilState(dsDesc);
        dsDesc->release();
    }

    auto *handle = new MetalRenderPipelineData{ pipelineState, depthStencilState };
    
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    deviceData->renderPipelineCount++;
    
    return std::shared_ptr<RenderPipeline>(new RenderPipeline(handle));
}

std::shared_ptr<ComputePipeline> Device::createComputePipeline(const ComputePipelineDescriptor &descriptor) {
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    if (!descriptor.compute.module) return nullptr;

    auto *smData   = static_cast<MetalShaderModuleData *>(descriptor.compute.module->native);
    dispatch_data_t dispData = dispatch_data_create(
        smData->bytes.data(), smData->bytes.size(), nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    NS::Error *compileError = nullptr;
    auto *library = dev->newLibrary(dispData, &compileError);
    dispatch_release(dispData);
    if (!library) {
        if (compileError) std::cerr << "createComputePipeline: "
                                    << compileError->localizedDescription()->utf8String() << std::endl;
        return nullptr;
    }
    auto *funcName = NS::String::string(descriptor.compute.entryPoint.c_str(), NS::UTF8StringEncoding);
    auto *function = library->newFunction(funcName);
    library->release();
    if (!function) return nullptr;

    NS::Error *error      = nullptr;
    auto *pipelineState   = dev->newComputePipelineState(function, &error);
    function->release();
    if (!pipelineState) {
        if (error) std::cerr << "createComputePipeline: "
                             << error->localizedDescription()->utf8String() << std::endl;
        return nullptr;
    }
    
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    deviceData->computePipelineCount++;
    
    return std::shared_ptr<ComputePipeline>(new ComputePipeline(pipelineState));
}

std::shared_ptr<BindGroupLayout> Device::createBindGroupLayout(const BindGroupLayoutDescriptor &descriptor) {
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    deviceData->bindGroupLayoutCount++;
    auto *data = new MetalBindGroupLayoutData{ descriptor.entries };
    return std::shared_ptr<BindGroupLayout>(new BindGroupLayout(data));
}

std::shared_ptr<BindGroup> Device::createBindGroup(const BindGroupDescriptor &descriptor) {
    auto *data = new MetalBindGroupData{};
    data->entries = descriptor.entries;
    if (descriptor.layout && descriptor.layout->native) {
        auto *layoutData = static_cast<MetalBindGroupLayoutData *>(descriptor.layout->native);
        for (const auto &entry : layoutData->entries) {
            data->visibility[entry.binding] = entry.visibility;
        }
    }

    auto *deviceData = static_cast<MetalDeviceData *>(native);
    deviceData->bindGroupCount++;

    return std::shared_ptr<BindGroup>(new BindGroup(data));
}

std::shared_ptr<PipelineLayout> Device::createPipelineLayout(const PipelineLayoutDescriptor &descriptor) {
    // Metal uses implicit pipeline layout; no separate object is required.
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    deviceData->pipelineLayoutCount++;
    return std::shared_ptr<PipelineLayout>(new PipelineLayout(nullptr));
}

std::shared_ptr<Sampler> Device::createSampler(const SamplerDescriptor &descriptor) {
    auto *dev  = static_cast<MetalDeviceData *>(native)->device;
    auto *desc = MTL::SamplerDescriptor::alloc()->init();

    desc->setSAddressMode(toMTLAddressMode(descriptor.addressModeU));
    desc->setTAddressMode(toMTLAddressMode(descriptor.addressModeV));
    desc->setRAddressMode(toMTLAddressMode(descriptor.addressModeW));
    desc->setMinFilter(toMTLFilter(descriptor.minFilter));
    desc->setMagFilter(toMTLFilter(descriptor.magFilter));
    desc->setMipFilter(toMTLMipFilter(descriptor.minFilter));
    desc->setLodMinClamp((float)descriptor.lodMinClamp);
    desc->setLodMaxClamp((float)descriptor.lodMaxClamp);
    desc->setMaxAnisotropy((NS::UInteger)std::max(1.0, descriptor.maxAnisotropy));
    if (descriptor.compare) {
        desc->setCompareFunction(toMTLCompare(*descriptor.compare));
    }

    auto *samplerState = dev->newSamplerState(desc);
    desc->release();
    if (!samplerState) return nullptr;
    
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    deviceData->samplerCount++;
    
    return std::shared_ptr<Sampler>(new Sampler(samplerState));
}

std::shared_ptr<QuerySet> Device::createQuerySet(const QuerySetDescriptor &descriptor) {
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    // Use a shared buffer to store occlusion/timestamp query results (8 bytes per slot).
    auto *buffer = dev->newBuffer(
        descriptor.count * sizeof(uint64_t), MTL::ResourceStorageModeShared);
    if (!buffer) return nullptr;
    
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    deviceData->querySetCount++;
    
    return std::shared_ptr<QuerySet>(new QuerySet(buffer));
}

std::shared_ptr<CommandEncoder> Device::createCommandEncoder() {
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    auto *cmdBuffer  = deviceData->commandQueue->commandBuffer();
    if (!cmdBuffer) return nullptr;
    cmdBuffer->retain();
    return std::shared_ptr<CommandEncoder>(new CommandEncoder(cmdBuffer));
}

void Device::submit(std::shared_ptr<CommandBuffer> commandBuffer) {
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    auto *cmdBuf     = static_cast<MTL::CommandBuffer *>(commandBuffer->native);

    // If a drawable was scheduled via scheduleNextPresent(), attach it to this
    // command buffer before committing.  Metal then presents the drawable at
    // the next vsync AFTER the GPU finishes, preventing "present before render"
    // artefacts.  The pointer is cleared after each use.
    if (deviceData->pendingPresentDrawable) {
        auto *drawable = static_cast<MTL::Drawable *>(deviceData->pendingPresentDrawable);
        cmdBuf->presentDrawable(drawable);
        deviceData->pendingPresentDrawable = nullptr;
    }

    cmdBuf->commit();
    deviceData->commandsSubmitted++;
}

void Device::submit(std::shared_ptr<CommandBuffer> commandBuffer,
                    std::shared_ptr<Fence> signalFence) {
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    auto *cmdBuf     = static_cast<MTL::CommandBuffer *>(commandBuffer->native);

    if (deviceData->pendingPresentDrawable) {
        auto *drawable = static_cast<MTL::Drawable *>(deviceData->pendingPresentDrawable);
        cmdBuf->presentDrawable(drawable);
        deviceData->pendingPresentDrawable = nullptr;
    }

    // Capture shared_ptr so the fence stays alive until the GPU finishes.
    cmdBuf->addCompletedHandler([signalFence](MTL::CommandBuffer*) {
        auto *fenceData = static_cast<MetalFenceData *>(signalFence->native);
        fenceData->signal();
    });

    cmdBuf->commit();
    deviceData->commandsSubmitted++;
}

void Device::scheduleNextPresent(void* nativeDrawable) {
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    deviceData->pendingPresentDrawable = nativeDrawable;
}

void Device::waitForIdle() {
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    // Commit an empty sentinel command buffer on the same queue and wait for it.
    // Metal executes command buffers in order within a queue, so when this
    // sentinel completes all previously committed buffers are guaranteed done.
    // commandBuffer() returns an autoreleased object; retain() to take ownership,
    // then release() after we're finished (mirrors createCommandEncoder pattern).
    MTL::CommandBuffer *sentinel = deviceData->commandQueue->commandBuffer();
    if (sentinel) {
        sentinel->retain();
        sentinel->commit();
        sentinel->waitUntilCompleted();
        sentinel->release();
    }
}

std::shared_ptr<Fence> Device::createFence() {
    auto *fenceData = new MetalFenceData();
    return std::shared_ptr<Fence>(new Fence(fenceData));
}

std::shared_ptr<TextureView> Device::getSwapchainTextureView() {
    // On Metal the swapchain is managed by MTKView. Use TextureView::fromNative()
    // with the CAMetalDrawable's texture instead.
    return nullptr;
}

std::string getVersion() {
    return std::to_string(campello_gpu_VERSION_MAJOR) + "." +
           std::to_string(campello_gpu_VERSION_MINOR) + "." +
           std::to_string(campello_gpu_VERSION_PATCH);
}

// ---------------------------------------------------------------------------
// Ray tracing factory methods
// ---------------------------------------------------------------------------

std::shared_ptr<AccelerationStructure> Device::createBottomLevelAccelerationStructure(
    const BottomLevelAccelerationStructureDescriptor &descriptor)
{
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    if (!dev->supportsRaytracing()) return nullptr;
    if (descriptor.geometries.empty()) return nullptr;

    auto bufToMTL = [](const std::shared_ptr<Buffer> &b) -> MTL::Buffer * {
        if (!b) return nullptr;
        auto *handle = static_cast<MetalBufferHandle *>(b->native);
        return handle ? handle->buffer : nullptr;
    };
    auto *primDesc = makePrimASDescriptor(descriptor, bufToMTL);
    auto sizes     = dev->accelerationStructureSizes(primDesc);
    auto *as       = dev->newAccelerationStructure(sizes.accelerationStructureSize);
    primDesc->release();

    if (!as) return nullptr;

    auto *handle = new MetalAccelerationStructureData{
        as,
        static_cast<uint64_t>(sizes.buildScratchBufferSize),
        static_cast<uint64_t>(sizes.refitScratchBufferSize)
    };
    
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    deviceData->accelerationStructureCount++;
    
    return std::shared_ptr<AccelerationStructure>(new AccelerationStructure(handle));
}

std::shared_ptr<AccelerationStructure> Device::createTopLevelAccelerationStructure(
    const TopLevelAccelerationStructureDescriptor &descriptor)
{
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    if (!dev->supportsRaytracing()) return nullptr;
    if (descriptor.instances.empty()) return nullptr;

    auto asToMTLData = [](const std::shared_ptr<AccelerationStructure> &a) {
        return static_cast<MetalAccelerationStructureData *>(a->native);
    };
    auto [instanceDesc, instanceBuf] = makeInstanceASDescriptor(dev, descriptor, asToMTLData);
    if (!instanceDesc) return nullptr;

    auto sizes = dev->accelerationStructureSizes(instanceDesc);
    auto *as   = dev->newAccelerationStructure(sizes.accelerationStructureSize);
    instanceDesc->release();
    instanceBuf->release();

    if (!as) return nullptr;

    auto *handle = new MetalAccelerationStructureData{
        as,
        static_cast<uint64_t>(sizes.buildScratchBufferSize),
        static_cast<uint64_t>(sizes.refitScratchBufferSize)
    };
    
    auto *deviceData = static_cast<MetalDeviceData *>(native);
    deviceData->accelerationStructureCount++;
    
    return std::shared_ptr<AccelerationStructure>(new AccelerationStructure(handle));
}

std::shared_ptr<RayTracingPipeline> Device::createRayTracingPipeline(
    const RayTracingPipelineDescriptor &descriptor)
{
    auto *dev = static_cast<MetalDeviceData *>(native)->device;
    if (!dev->supportsRaytracing()) return nullptr;
    if (!descriptor.rayGeneration.module) return nullptr;

    // Helper: compile a ShaderModule to a MTL::Library.
    auto compileLib = [&](ShaderModule *sm) -> MTL::Library * {
        auto *data = static_cast<MetalShaderModuleData *>(sm->native);
        dispatch_data_t dd = dispatch_data_create(
            data->bytes.data(), data->bytes.size(), nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        NS::Error *err = nullptr;
        auto *lib = dev->newLibrary(dd, &err);
        dispatch_release(dd);
        if (!lib && err)
            std::cerr << "createRayTracingPipeline: "
                      << err->localizedDescription()->utf8String() << std::endl;
        return lib;
    };

    // Compile ray generation shader.
    auto *rgLib = compileLib(descriptor.rayGeneration.module.get());
    if (!rgLib) return nullptr;
    auto *rgFuncName = NS::String::string(
        descriptor.rayGeneration.entryPoint.c_str(), NS::UTF8StringEncoding);
    auto *rgFunc = rgLib->newFunction(rgFuncName);
    rgLib->release();
    if (!rgFunc) return nullptr;

    // Collect intersection functions from hit groups.
    // Store (hitGroupIndex, MTL::Function*) pairs for IFT population later.
    struct IntersectionEntry { size_t index; MTL::Function *func; };
    std::vector<IntersectionEntry> isFuncs;

    std::vector<NS::Object *> linkedFuncObjects;
    for (size_t i = 0; i < descriptor.hitGroups.size(); i++) {
        const auto &hg = descriptor.hitGroups[i];
        if (!hg.intersection || !hg.intersection->module) continue;

        auto *isLib = compileLib(hg.intersection->module.get());
        if (!isLib) continue;
        auto *isFuncName = NS::String::string(
            hg.intersection->entryPoint.c_str(), NS::UTF8StringEncoding);
        auto *isFunc = isLib->newFunction(isFuncName);
        isLib->release();
        if (!isFunc) continue;

        linkedFuncObjects.push_back(isFunc);
        isFuncs.push_back({ i, isFunc });
    }

    // Build compute pipeline descriptor.
    auto *pipelineDesc = MTL::ComputePipelineDescriptor::alloc()->init();
    pipelineDesc->setComputeFunction(rgFunc);
    rgFunc->release();

    if (!linkedFuncObjects.empty()) {
        auto *linkedFuncsArray = NS::Array::array(
            linkedFuncObjects.data(),
            static_cast<NS::UInteger>(linkedFuncObjects.size()));
        auto *linked = MTL::LinkedFunctions::alloc()->init();
        linked->setFunctions(linkedFuncsArray);
        pipelineDesc->setLinkedFunctions(linked);
        linked->release();
    }

    NS::Error *err = nullptr;
    auto *pipelineState = dev->newComputePipelineState(
        pipelineDesc, MTL::PipelineOptionNone, nullptr, &err);
    pipelineDesc->release();
    if (!pipelineState) {
        if (err)
            std::cerr << "createRayTracingPipeline PSO: "
                      << err->localizedDescription()->utf8String() << std::endl;
        for (auto &e : isFuncs) e.func->release();
        return nullptr;
    }

    // Create intersection function table if any custom intersection shaders exist.
    MTL::IntersectionFunctionTable *ift = nullptr;
    if (!isFuncs.empty()) {
        auto *iftDesc = MTL::IntersectionFunctionTableDescriptor::alloc()->init();
        iftDesc->setFunctionCount(descriptor.hitGroups.size());
        ift = pipelineState->newIntersectionFunctionTable(iftDesc);
        iftDesc->release();

        if (ift) {
            for (auto &e : isFuncs) {
                auto *handle = pipelineState->functionHandle(e.func);
                if (handle)
                    ift->setFunction(handle, static_cast<NS::UInteger>(e.index));
            }
        }
    }
    for (auto &e : isFuncs) e.func->release();

    auto *rtHandle = new MetalRayTracingPipelineData{
        pipelineState,
        ift,
        pipelineState->threadExecutionWidth(),
        pipelineState->maxTotalThreadsPerThreadgroup()
    };
    
    auto *deviceData2 = static_cast<MetalDeviceData *>(native);
    deviceData2->rayTracingPipelineCount++;
    
    return std::shared_ptr<RayTracingPipeline>(new RayTracingPipeline(rtHandle));
}
