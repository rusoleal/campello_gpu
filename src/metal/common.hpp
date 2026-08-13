#pragma once

#include "Metal.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <campello_gpu/metrics.hpp>
#include <campello_gpu/constants/pixel_format.hpp>

// Forward declarations for Metal classes
namespace MTL {
    class Device;
    class CommandQueue;
    class Buffer;
    class Texture;
}

namespace systems::leal::campello_gpu {

/**
 * @brief RAII wrapper around NSAutoreleasePool.
 *
 * The Metal-cpp wrapper follows Objective-C memory rules: methods whose names do
 * not begin with alloc/new/copy return autoreleased objects. Public campello_gpu
 * entry points instantiate this helper so those transient objects are drained
 * before returning to the caller, preventing leaks when the caller has no active
 * autorelease pool (background threads, dispatch queues, non-AppKit consumers).
 */
class MetalAutoreleasePool {
    NS::AutoreleasePool *pool;
public:
    MetalAutoreleasePool()
        : pool(NS::AutoreleasePool::alloc()->init()) {}
    ~MetalAutoreleasePool() {
        if (pool) pool->drain();
    }

    MetalAutoreleasePool(const MetalAutoreleasePool&) = delete;
    MetalAutoreleasePool& operator=(const MetalAutoreleasePool&) = delete;
    MetalAutoreleasePool(MetalAutoreleasePool&&) = delete;
    MetalAutoreleasePool& operator=(MetalAutoreleasePool&&) = delete;
};

/**
 * @brief Metal-specific fence data — pure CPU-side synchronization.
 *
 * Metal does not expose lightweight binary fences, so we use a condition
 * variable signaled from the command buffer's addCompletedHandler.
 */
struct MetalFenceData {
    std::atomic<bool> signaled{true};  // start signaled so first frame doesn't block
    // Set from the command buffer's addCompletedHandler when its status is
    // MTLCommandBufferStatusError (e.g. a driver-level GPU timeout under load)
    // instead of Completed. Metal invokes completed handlers on failure just
    // as on success, so without this a failed submission would signal the
    // fence exactly like a successful one -- the caller's wait() would return
    // normally and it would read back whatever was already sitting in the
    // destination buffer (stale data from a prior submission, not this one's
    // actual output) with no indication anything went wrong.
    std::atomic<bool> failed{false};
    // Human-readable reason for `failed`, e.g. "Caused GPU Timeout Error
    // (...)" from MTLCommandBuffer::error()->localizedDescription() -- set
    // once, before `failed` flips true and before signal(), so it's safe to
    // read after observing `failed` without additional locking.
    std::string failureReason;
    mutable std::mutex mutex;
    std::condition_variable cv;

    void signal() {
        signaled.store(true, std::memory_order_release);
        cv.notify_all();
    }
};

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

/**
 * @brief Translates a campello_gpu PixelFormat to its Metal equivalent.
 *
 * campello_gpu::PixelFormat values are numbered to match MTL::PixelFormat
 * directly wherever a real Metal format exists, so most formats are a raw
 * cast. `depth24plus_stencil8` is the exception: it maps to
 * MTLPixelFormatDepth24Unorm_Stencil8, an *optional* Metal format
 * (gated by `isDepth24Stencil8PixelFormatSupported`) that most Apple
 * Silicon / paravirtualized devices don't support. Per WebGPU semantics,
 * "depth24plus" only promises *at least* 24 bits of depth, so on devices
 * without the optional format we substitute the always-supported
 * Depth32Float_Stencil8 instead of handing Metal an unusable format.
 */
inline MTL::PixelFormat toMTLPixelFormat(MTL::Device *device, PixelFormat format) {
    if (format == PixelFormat::depth24plus_stencil8 &&
        device && !device->depth24Stencil8PixelFormatSupported()) {
        return MTL::PixelFormatDepth32Float_Stencil8;
    }
    return (MTL::PixelFormat)format;
}

} // namespace systems::leal::campello_gpu
