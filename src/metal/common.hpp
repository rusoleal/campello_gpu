#pragma once

#include <atomic>
#include <cstdint>
#include <campello_gpu/metrics.hpp>

// Forward declarations for Metal classes
namespace MTL {
    class Device;
    class CommandQueue;
    class Buffer;
    class Texture;
}

namespace systems::leal::campello_gpu {

/**
 * @brief Metal-specific device data structure.
 * 
 * This contains all the Metal device state including counters and metrics.
 * It's defined in a header so that both device.cpp and handle headers can use it.
 */
struct MetalDeviceData {
    MTL::Device       *device;
    MTL::CommandQueue *commandQueue;

    // Drawable scheduled via Device::scheduleNextPresent() — consumed on the
    // next Device::submit() call to attach presentDrawable: before commit().
    void* pendingPresentDrawable = nullptr;

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
    
    // GPU timestamp calibration (for converting GPU ticks to nanoseconds)
    std::atomic<uint64_t> gpuTimestampNumerator{1};
    std::atomic<uint64_t> gpuTimestampDenominator{1};
    
    // Timestamp query buffer for pass-level timing
    MTL::Buffer* timestampQueryBuffer = nullptr;  // Stores GPU timestamps
    std::atomic<uint32_t> nextTimestampQueryIndex{0};
    
    // Phase 3: Memory budget and pressure management
    MemoryBudget memoryBudget;
    MemoryPressureCallback memoryPressureCallback;
    std::atomic<MemoryPressureLevel> lastPressureLevel{MemoryPressureLevel::Normal};
};

} // namespace systems::leal::campello_gpu
