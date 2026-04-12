#pragma once

#include <cstdint>
#include <functional>

namespace systems::leal::campello_gpu {

/**
 * @brief Device-level memory information from the driver.
 * 
 * Note: Not all fields are available on all backends. Fields that cannot
 * be queried will be set to 0.
 */
struct DeviceMemoryInfo {
    /** Total dedicated VRAM in bytes (discrete GPUs). 0 if unknown or unified memory. */
    uint64_t totalDeviceMemory = 0;
    
    /** Currently available VRAM in bytes. 0 if unknown. */
    uint64_t availableDeviceMemory = 0;
    
    /** Driver-reported current allocation size in bytes. 0 if not supported. */
    uint64_t currentAllocatedSize = 0;
    
    /** Recommended maximum working set size for optimal performance. 0 if not supported. */
    uint64_t recommendedMaxWorkingSet = 0;
    
    /** True if GPU shares memory with CPU (integrated/Apple Silicon). */
    bool hasUnifiedMemory = false;
};

/**
 * @brief Resource creation counters (useful for debugging leaks/perf).
 * 
 * These counters track the number of live resources created through
 * this Device instance. They increment on creation and decrement on
 * resource destruction.
 */
struct ResourceCounters {
    uint32_t bufferCount = 0;
    uint32_t textureCount = 0;
    uint32_t renderPipelineCount = 0;
    uint32_t computePipelineCount = 0;
    uint32_t rayTracingPipelineCount = 0;
    uint32_t accelerationStructureCount = 0;
    uint32_t shaderModuleCount = 0;
    uint32_t samplerCount = 0;
    uint32_t bindGroupCount = 0;
    uint32_t bindGroupLayoutCount = 0;
    uint32_t pipelineLayoutCount = 0;
    uint32_t querySetCount = 0;
};

/**
 * @brief Command submission statistics.
 * 
 * These counters accumulate since device creation or the last call to
 * resetCommandStats(). They track work submitted to the GPU.
 */
struct CommandStats {
    /** Total number of command buffers submitted via submit(). */
    uint64_t commandsSubmitted = 0;
    
    /** Number of render passes recorded. */
    uint64_t renderPasses = 0;
    
    /** Number of compute passes recorded. */
    uint64_t computePasses = 0;
    
    /** Number of ray tracing passes recorded. */
    uint64_t rayTracingPasses = 0;
    
    /** Total draw calls (draw, drawIndexed, drawIndirect, drawIndexedIndirect). */
    uint64_t drawCalls = 0;
    
    /** Total compute dispatches (dispatchWorkgroups, dispatchIndirect). */
    uint64_t dispatchCalls = 0;
    
    /** Total ray tracing dispatches (traceRays). */
    uint64_t traceRaysCalls = 0;
    
    /** Total copy operations (copyBuffer, copyTexture, etc.). */
    uint64_t copies = 0;
};

/**
 * @brief Memory usage statistics per resource type (Phase 2).
 * 
 * These counters track the actual bytes allocated for GPU resources.
 * They provide a breakdown of where memory is being consumed.
 */
struct ResourceMemoryStats {
    /** Current bytes allocated for buffers. */
    uint64_t bufferBytes = 0;
    
    /** Current bytes allocated for textures (including mipmaps). */
    uint64_t textureBytes = 0;
    
    /** Current bytes allocated for acceleration structures. */
    uint64_t accelerationStructureBytes = 0;
    
    /** Current bytes allocated for shader modules (metadata only, not GPU memory). */
    uint64_t shaderModuleBytes = 0;
    
    /** Current bytes allocated for query sets and result buffers. */
    uint64_t querySetBytes = 0;
    
    /** Total GPU memory tracked across all resource types. */
    uint64_t totalTrackedBytes = 0;
    
    // ------------------------------------------------------------------
    // Peak tracking (Phase 2 enhanced)
    // ------------------------------------------------------------------
    
    /** Peak buffer allocation observed since device creation. */
    uint64_t peakBufferBytes = 0;
    
    /** Peak texture allocation observed since device creation. */
    uint64_t peakTextureBytes = 0;
    
    /** Peak acceleration structure allocation observed. */
    uint64_t peakAccelerationStructureBytes = 0;
    
    /** Peak total GPU memory observed since device creation. */
    uint64_t peakTotalBytes = 0;
};

/**
 * @brief Complete metrics snapshot (convenience aggregate).
 */
struct Metrics {
    DeviceMemoryInfo deviceMemory;
    ResourceCounters resources;
    CommandStats commands;
    ResourceMemoryStats resourceMemory;
};

// ------------------------------------------------------------------------------
// Phase 3: GPU Timing and Performance Profiling
// ------------------------------------------------------------------------------

/**
 * @brief GPU timestamp query result (nanoseconds).
 * 
 * Timestamp queries measure GPU execution time for specific operations.
 * Resolution varies by hardware (typically ~80-1000 nanoseconds).
 */
struct GPUTimestamp {
    /** Timestamp value in nanoseconds from GPU clock. */
    uint64_t nanoseconds = 0;
    
    /** True if the timestamp has been resolved from the GPU. */
    bool valid = false;
};

/**
 * @brief GPU timing statistics for a completed time interval.
 * 
 * Represents the duration between two GPU timestamps (end - start).
 */
struct GPUTimingInterval {
    /** Duration in nanoseconds. */
    uint64_t durationNs = 0;
    
    /** Duration in milliseconds (convenience). */
    double durationMs = 0.0;
    
    /** True if both timestamps were valid. */
    bool valid = false;
};

/**
 * @brief Performance statistics for a single render/compute/raytracing pass.
 * 
 * Tracks GPU time for a specific pass instance. These are accumulated
 * into PassPerformanceStats.
 */
struct PassTiming {
    /** Pass type identifier. */
    enum Type { Render, Compute, RayTracing } type;
    
    /** GPU time for this pass instance (nanoseconds). */
    uint64_t gpuTimeNs = 0;
    
    /** Whether this timing sample is valid. */
    bool valid = false;
};

/**
 * @brief Aggregated performance statistics for passes (Phase 3).
 * 
 * These counters accumulate GPU timing data for different pass types.
 * They track total GPU time spent in each pass category.
 */
struct PassPerformanceStats {
    /** Total GPU time spent in render passes (nanoseconds). */
    uint64_t renderPassTimeNs = 0;
    
    /** Total GPU time spent in compute passes (nanoseconds). */
    uint64_t computePassTimeNs = 0;
    
    /** Total GPU time spent in ray tracing passes (nanoseconds). */
    uint64_t rayTracingPassTimeNs = 0;
    
    /** Total GPU time for all passes (nanoseconds). */
    uint64_t totalPassTimeNs = 0;
    
    /** Number of render pass timing samples collected. */
    uint32_t renderPassSampleCount = 0;
    
    /** Number of compute pass timing samples collected. */
    uint32_t computePassSampleCount = 0;
    
    /** Number of ray tracing pass timing samples collected. */
    uint32_t rayTracingPassSampleCount = 0;
    
    /** Average render pass time (nanoseconds), 0 if no samples. */
    uint64_t averageRenderPassTimeNs() const {
        return renderPassSampleCount > 0 ? renderPassTimeNs / renderPassSampleCount : 0;
    }
    
    /** Average compute pass time (nanoseconds), 0 if no samples. */
    uint64_t averageComputePassTimeNs() const {
        return computePassSampleCount > 0 ? computePassTimeNs / computePassSampleCount : 0;
    }
    
    /** Average ray tracing pass time (nanoseconds), 0 if no samples. */
    uint64_t averageRayTracingPassTimeNs() const {
        return rayTracingPassSampleCount > 0 ? rayTracingPassTimeNs / rayTracingPassSampleCount : 0;
    }
};

// ------------------------------------------------------------------------------
// Phase 3: Memory Pressure and Budget Management
// ------------------------------------------------------------------------------

/**
 * @brief Memory pressure level (Phase 3).
 * 
 * Indicates the severity of memory pressure. Used by callbacks and
 * automatic resource management.
 */
enum class MemoryPressureLevel {
    /** Normal - memory usage below warning threshold. */
    Normal = 0,
    
    /** Warning - approaching memory limit, consider cleanup. */
    Warning = 1,
    
    /** Critical - near memory limit, aggressive eviction recommended. */
    Critical = 2
};

/**
 * @brief Memory budget configuration (Phase 3).
 * 
 * Configures thresholds for memory pressure detection and automatic
 * resource management.
 */
struct MemoryBudget {
    /** 
     * Warning threshold as percentage of recommended working set (1-100).
     * Default: 80%
     */
    uint32_t warningThresholdPercent = 80;
    
    /**
     * Critical threshold as percentage of recommended working set (1-100).
     * Default: 95%
     */
    uint32_t criticalThresholdPercent = 95;
    
    /** 
     * Target memory usage after automatic eviction (percentage of budget).
     * Default: 70%
     */
    uint32_t targetUsagePercent = 70;
    
    /** Enable automatic resource eviction when pressure is critical. */
    bool enableAutomaticEviction = false;
};

/**
 * @brief Memory pressure callback signature (Phase 3).
 * 
 * Called when memory pressure changes. The callback receives the current
 * pressure level and memory stats. This is for monitoring - the callback
 * should not block or perform heavy operations.
 * 
 * @param level Current memory pressure level
 * @param stats Current resource memory statistics
 */
using MemoryPressureCallback = std::function<void(MemoryPressureLevel level, const ResourceMemoryStats& stats)>;

/**
 * @brief Complete Phase 3 metrics snapshot with performance data.
 * 
 * Extends the base Metrics struct with GPU timing information.
 */
struct MetricsWithTiming : public Metrics {
    /** GPU timing statistics for passes. */
    PassPerformanceStats passPerformance;
};

} // namespace systems::leal::campello_gpu
