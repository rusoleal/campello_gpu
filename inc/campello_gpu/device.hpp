#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/shader_module.hpp>
#include <campello_gpu/bind_group_layout.hpp>
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
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/storage_mode.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/feature.hpp>
#include <campello_gpu/metrics.hpp>

namespace systems::leal::campello_gpu
{

    class Adapter;

    /**
     * @brief Central GPU device — the primary entry point for all campello_gpu operations.
     *
     * A `Device` is a logical handle to a GPU adapter. It is the factory for every
     * GPU resource: buffers, textures, shader modules, pipelines, samplers, query sets,
     * bind groups, and command encoders. It also submits completed command buffers to
     * the GPU for execution.
     *
     * **Typical initialization:**
     * @code
     * // Option 1 — default device (picks the system's primary GPU)
     * auto device = Device::createDefaultDevice(nullptr);
     *
     * // Option 2 — choose a specific adapter
     * auto adapters = Device::getAdapters();
     * auto device   = Device::createDevice(adapters[0], nullptr);
     * @endcode
     *
     * The `void* pd` parameter accepted by factory methods is an optional
     * platform-specific window/surface handle used by some backends during device
     * creation (e.g. `ANativeWindow*` on Android). Pass `nullptr` when no surface
     * is needed at device creation time.
     */
    class Device
    {
    private:
        void *native;

        Device(void *data);

    public:
        ~Device();

        // ------------------------------------------------------------------
        // Device information
        // ------------------------------------------------------------------

        /**
         * @brief Returns a human-readable name for the GPU adapter.
         * @return Adapter name string (e.g. "Apple M2" or "Intel UHD Graphics 630").
         */
        std::string getName();

        /**
         * @brief Returns the set of optional features supported by this device.
         *
         * Features must be queried before use; relying on unsupported features is
         * undefined behaviour.
         *
         * @return Set of `Feature` enum values available on this device.
         */
        std::set<Feature> getFeatures();

        // ------------------------------------------------------------------
        // Metrics and monitoring (Phase 1)
        // ------------------------------------------------------------------

        /**
         * @brief Returns current GPU memory metrics from the driver.
         *
         * Note: Not all backends support all fields. Fields that cannot be queried
         * will be set to 0. Check hasUnifiedMemory to understand memory architecture.
         *
         * @return DeviceMemoryInfo with driver-reported memory statistics.
         */
        DeviceMemoryInfo getMemoryInfo();

        /**
         * @brief Returns current resource creation counters.
         *
         * These counters track live resources created through this Device.
         * They increment on creation and decrement on destruction.
         *
         * @return ResourceCounters with current resource counts.
         */
        ResourceCounters getResourceCounters();

        /**
         * @brief Returns command submission statistics.
         *
         * These counters accumulate since device creation or the last reset.
         *
         * @return CommandStats with accumulated command counts.
         */
        CommandStats getCommandStats();

        /**
         * @brief Returns a complete metrics snapshot (convenience method).
         *
         * Equivalent to querying getMemoryInfo(), getResourceCounters(), and
         * getCommandStats() in a single call.
         *
         * @return Metrics struct with all available metrics.
         */
        Metrics getMetrics();

        /**
         * @brief Resets command statistics counters to zero.
         *
         * This resets all counters in CommandStats. Resource counters and
         * memory info are not affected.
         */
        void resetCommandStats();

        // ------------------------------------------------------------------
        // Metrics and monitoring (Phase 2 - Enhanced)
        // ------------------------------------------------------------------

        /**
         * @brief Returns detailed resource memory statistics.
         *
         * Provides per-resource-type byte counts and peak allocation tracking.
         * Useful for memory profiling and leak detection.
         *
         * @return ResourceMemoryStats with current and peak memory usage per type.
         */
        ResourceMemoryStats getResourceMemoryStats();

        /**
         * @brief Resets peak memory tracking counters.
         *
         * This resets all peak_* fields in ResourceMemoryStats to their
         * current values. Useful for measuring memory spikes over specific
         * time periods or operations.
         */
        void resetPeakMemoryStats();

        // ------------------------------------------------------------------
        // Metrics and monitoring (Phase 3 - Advanced)
        // ------------------------------------------------------------------

        /**
         * @brief Returns GPU timing statistics for passes.
         *
         * Provides accumulated GPU time for render, compute, and ray tracing
         * passes. Times are in nanoseconds. Use resetPassPerformanceStats()
         * to start a new measurement period.
         *
         * @return PassPerformanceStats with GPU timing data.
         */
        PassPerformanceStats getPassPerformanceStats();

        /**
         * @brief Resets GPU pass timing statistics to zero.
         *
         * This resets all timing counters in PassPerformanceStats. Call this
         * before starting a new profiling period.
         */
        void resetPassPerformanceStats();

        /**
         * @brief Gets the current memory pressure level.
         *
         * Compares current memory usage against configured budget thresholds
         * to determine if the system is under memory pressure.
         *
         * @return MemoryPressureLevel indicating current pressure state.
         */
        MemoryPressureLevel getMemoryPressureLevel();

        /**
         * @brief Configures memory budget and automatic resource management.
         *
         * Sets thresholds for memory pressure detection and configures
         * automatic resource eviction behavior. The budget is applied
         * immediately.
         *
         * @param budget MemoryBudget configuration structure.
         */
        void setMemoryBudget(const MemoryBudget& budget);

        /**
         * @brief Gets the current memory budget configuration.
         *
         * @return Current MemoryBudget settings.
         */
        MemoryBudget getMemoryBudget();

        /**
         * @brief Registers a callback for memory pressure events.
         *
         * The callback is invoked when memory pressure changes (Normal ->
         * Warning, Warning -> Critical, etc.). Only one callback can be
         * registered; subsequent calls replace the previous callback.
         *
         * Pass nullptr to unregister the callback.
         *
         * @param callback MemoryPressureCallback function or nullptr.
         */
        void setMemoryPressureCallback(MemoryPressureCallback callback);

        /**
         * @brief Manually triggers memory pressure check and eviction.
         *
         * Forces a memory pressure evaluation. If pressure is Critical and
         * automatic eviction is enabled, this may evict resources. Returns
         * the current pressure level after any eviction.
         *
         * @return MemoryPressureLevel after handling pressure.
         */
        MemoryPressureLevel checkMemoryPressure();

        /**
         * @brief Returns complete metrics with GPU timing data.
         *
         * Equivalent to getMetrics() plus GPU pass performance stats.
         * This is a convenience method for comprehensive profiling.
         *
         * @return MetricsWithTiming with all metrics and timing data.
         */
        MetricsWithTiming getMetricsWithTiming();

        // ------------------------------------------------------------------
        // Resource creation
        // ------------------------------------------------------------------

        /**
         * @brief Creates a GPU texture.
         *
         * @param type       Dimensionality: `tt1d`, `tt2d`, or `tt3d`.
         * @param pixelFormat Data format of each texel (e.g. `rgba8unorm`).
         * @param width      Width in texels.
         * @param height     Height in texels (ignored for 1D textures).
         * @param depth      Depth in texels for 3D textures, or array layer count
         *                   for array textures.
         * @param mipLevels  Number of mip-map levels (1 = no mip chain).
         * @param samples    MSAA sample count (1 = no multisampling).
         * @param usageMode  Bitfield of `TextureUsage` flags declaring how the texture
         *                   will be used (render target, sampled, storage, copy, etc.).
         * @return A new `Texture`, or `nullptr` on failure.
         */
        std::shared_ptr<Texture> createTexture(
            TextureType type, PixelFormat pixelFormat,
            uint32_t width, uint32_t height, uint32_t depth,
            uint32_t mipLevels, uint32_t samples,
            TextureUsage usageMode);

        /**
         * @brief Creates an uninitialised GPU buffer.
         *
         * @param size  Byte size of the buffer.
         * @param usage Bitfield of `BufferUsage` flags (vertex, index, uniform, etc.).
         * @return A new `Buffer`, or `nullptr` on failure.
         */
        std::shared_ptr<Buffer> createBuffer(uint64_t size, BufferUsage usage);

        /**
         * @brief Creates a GPU buffer and immediately uploads data into it.
         *
         * Equivalent to calling `createBuffer(size, usage)` followed by
         * `buffer->upload(0, size, data)`.
         *
         * @param size  Byte size of the buffer.
         * @param usage Bitfield of `BufferUsage` flags.
         * @param data  Pointer to the initial data (must be at least `size` bytes).
         * @return A new `Buffer` pre-filled with `data`, or `nullptr` on failure.
         */
        std::shared_ptr<Buffer> createBuffer(uint64_t size, BufferUsage usage, void *data);

        /**
         * @brief Creates a shader module from a compiled binary.
         *
         * The binary format is platform-specific:
         * - **Metal** — compiled `.metallib` (Metal Standard Library binary).
         * - **Vulkan** — SPIR-V binary.
         *
         * @param buffer Pointer to the compiled shader binary.
         * @param size   Byte length of the binary.
         * @return A new `ShaderModule`, or `nullptr` on failure.
         */
        std::shared_ptr<ShaderModule> createShaderModule(const uint8_t *buffer, uint64_t size);

        /**
         * @brief Creates a render pipeline state object.
         *
         * Compiles and links the vertex and fragment shaders together with their
         * input layout, blend state, depth/stencil configuration, and output formats
         * into a single GPU object. Create pipelines at load time to avoid runtime
         * stalls.
         *
         * @param descriptor Full description of the pipeline (see `RenderPipelineDescriptor`).
         * @return A new `RenderPipeline`, or `nullptr` on compilation failure.
         */
        std::shared_ptr<RenderPipeline> createRenderPipeline(const RenderPipelineDescriptor &descriptor);

        /**
         * @brief Creates a compute pipeline state object.
         *
         * @param descriptor Full description of the pipeline (see `ComputePipelineDescriptor`).
         * @return A new `ComputePipeline`, or `nullptr` on compilation failure.
         */
        std::shared_ptr<ComputePipeline> createComputePipeline(const ComputePipelineDescriptor &descriptor);

        /**
         * @brief Creates a bind group layout.
         *
         * A layout defines the structure (slots, types, visibility) of one group of
         * shader resource bindings. It is used as a template when creating `BindGroup`
         * objects and when building pipeline layouts.
         *
         * @param descriptor Description of the binding slots.
         * @return A new `BindGroupLayout`, or `nullptr` on failure.
         */
        std::shared_ptr<BindGroupLayout> createBindGroupLayout(const BindGroupLayoutDescriptor &descriptor);

        /**
         * @brief Creates a bind group from a layout and a set of concrete resources.
         *
         * The bind group's layout must match the layout expected by the pipeline that
         * will use it.
         *
         * @param descriptor Resources to bind and the layout they conform to.
         * @return A new `BindGroup`, or `nullptr` on failure.
         */
        std::shared_ptr<BindGroup> createBindGroup(const BindGroupDescriptor &descriptor);

        /**
         * @brief Creates a pipeline layout.
         *
         * A pipeline layout defines the complete set of `BindGroupLayout` objects used
         * by a pipeline across all shader stages.
         *
         * @param descriptor Bind group layouts that make up the pipeline layout.
         * @return A new `PipelineLayout`, or `nullptr` on failure.
         */
        std::shared_ptr<PipelineLayout> createPipelineLayout(const PipelineLayoutDescriptor &descriptor);

        /**
         * @brief Creates an immutable sampler state object.
         *
         * @param descriptor Filtering, wrap, LOD, and comparison configuration.
         * @return A new `Sampler`, or `nullptr` on failure.
         */
        std::shared_ptr<Sampler> createSampler(const SamplerDescriptor &descriptor);

        /**
         * @brief Creates a query set for occlusion or timestamp queries.
         *
         * @param descriptor Type and number of query slots.
         * @return A new `QuerySet`, or `nullptr` on failure.
         */
        std::shared_ptr<QuerySet> createQuerySet(const QuerySetDescriptor &descriptor);

        /**
         * @brief Creates a command encoder for recording GPU commands.
         *
         * Create one encoder per frame (or per submit). Record render/compute passes
         * and copy commands, then call `finish()` to get the `CommandBuffer` and
         * `submit()` to execute it.
         *
         * @return A new `CommandEncoder`, or `nullptr` on failure.
         */
        std::shared_ptr<CommandEncoder> createCommandEncoder();

        // ------------------------------------------------------------------
        // Ray tracing resource creation (requires Feature::raytracing)
        // ------------------------------------------------------------------

        /**
         * @brief Creates a bottom-level acceleration structure (BLAS).
         *
         * Allocates the GPU backing memory and computes the required scratch buffer
         * sizes for the given geometry. The BVH is not built yet — call
         * `CommandEncoder::buildAccelerationStructure()` with the same descriptor and
         * a scratch buffer of at least `result->getBuildScratchSize()` bytes.
         *
         * Requires `Feature::raytracing` to be present in `getFeatures()`.
         *
         * @param descriptor Geometry and build-hint configuration.
         * @return A new `AccelerationStructure` handle, or `nullptr` on failure.
         */
        std::shared_ptr<AccelerationStructure> createBottomLevelAccelerationStructure(
            const BottomLevelAccelerationStructureDescriptor &descriptor);

        /**
         * @brief Creates a top-level acceleration structure (TLAS).
         *
         * Allocates the GPU backing memory for the given instance list. The BVH is
         * not built until `CommandEncoder::buildAccelerationStructure()` is submitted.
         * All BLAS handles referenced by the instances must be fully built before
         * the TLAS build executes on the GPU.
         *
         * Requires `Feature::raytracing` to be present in `getFeatures()`.
         *
         * @param descriptor Instance list and build-hint configuration.
         * @return A new `AccelerationStructure` handle, or `nullptr` on failure.
         */
        std::shared_ptr<AccelerationStructure> createTopLevelAccelerationStructure(
            const TopLevelAccelerationStructureDescriptor &descriptor);

        /**
         * @brief Creates a compiled ray tracing pipeline state object.
         *
         * Compiles and links the ray generation, miss, and hit group shaders with
         * their pipeline layout and builds the platform-specific Shader Binding Table
         * (or equivalent). Create pipelines at load time to avoid runtime stalls.
         *
         * Requires `Feature::raytracing` to be present in `getFeatures()`.
         *
         * @param descriptor Shaders, layout, and recursion-depth configuration.
         * @return A new `RayTracingPipeline`, or `nullptr` on compilation failure.
         */
        std::shared_ptr<RayTracingPipeline> createRayTracingPipeline(
            const RayTracingPipelineDescriptor &descriptor);

        /**
         * @brief Submits a completed command buffer to the GPU for execution.
         *
         * The GPU will begin executing the recorded commands asynchronously. The
         * `commandBuffer` object should be released after this call.
         *
         * @param commandBuffer The command buffer obtained from `CommandEncoder::finish()`.
         */
        void submit(std::shared_ptr<CommandBuffer> commandBuffer);

        /**
         * @brief Schedules a platform drawable to be presented after the next
         *        submitted command buffer completes on the GPU.
         *
         * Call this once per frame, before the final `submit()` that renders to
         * the drawable's texture.  On Metal, this calls
         * `[MTLCommandBuffer presentDrawable:]` before `commit()`, which ties
         * presentation to the GPU completion signal and to the display vsync —
         * eliminating tearing and "present before render" artefacts that occur
         * when `[drawable present]` is called separately on the CPU.
         *
         * The stored pointer is consumed by the very next `submit()` call and
         * then cleared, so it must be set again every frame.
         *
         * On Vulkan and DirectX the function is a no-op (those backends handle
         * swapchain presentation inside `submit()` already).
         *
         * @param nativeDrawable  Platform drawable handle — on macOS/iOS this is
         *                        `(__bridge void*)id<CAMetalDrawable>`.
         */
        void scheduleNextPresent(void* nativeDrawable);

        /**
         * @brief Blocks the calling thread until all previously submitted commands
         *        on this device's queue have finished executing on the GPU.
         *
         * Use this to synchronize CPU and GPU when a texture rendered by one
         * `Device` must be fully written before another `Device` (or the display
         * compositor) reads it.  For example, call this after submitting an
         * offscreen render pass and before presenting the result to the screen.
         *
         * This is a CPU-side wait: it does not stall other GPU work on unrelated
         * queues, but it does block the calling thread until the GPU drains.
         * Avoid calling it every frame on a performance-critical path; prefer
         * event-based synchronization when possible.
         */
        void waitForIdle();

        // ------------------------------------------------------------------
        // Static factory methods
        // ------------------------------------------------------------------

        /**
         * @brief Creates a device on the system's default GPU adapter.
         *
         * Convenience wrapper that selects the primary adapter automatically.
         *
         * @param pd Platform-specific surface/window handle, or `nullptr`.
         * @return A new `Device`, or `nullptr` if no suitable adapter is found.
         */
        static std::shared_ptr<Device> createDefaultDevice(void *pd);

        /**
         * @brief Creates a device on a specific adapter.
         *
         * @param adapter The adapter returned by `getAdapters()`.
         * @param pd      Platform-specific surface/window handle, or `nullptr`.
         * @return A new `Device`, or `nullptr` on failure.
         */
        static std::shared_ptr<Device> createDevice(std::shared_ptr<Adapter> adapter, void *pd);

        /**
         * @brief Enumerates all available physical GPU adapters on the system.
         *
         * @return A vector of `Adapter` objects, one per physical GPU.
         *         Empty if no supported GPUs are found.
         */
        static std::vector<std::shared_ptr<Adapter>> getAdapters();

        /**
         * @brief Returns the campello_gpu library version string.
         * @return Version string in `MAJOR.MINOR.PATCH` format (e.g. `"0.3.1"`).
         */
        static std::string getEngineVersion();

        /**
         * @brief Returns a TextureView wrapping the current swapchain back buffer.
         *
         * Only valid when the device was created with a window handle (`pd != nullptr`).
         * Call this once per frame before beginning a render pass that targets the
         * swapchain. The view is valid until the next `submit()` call.
         *
         * @return A `TextureView` for the current back buffer, or `nullptr` if no
         *         swapchain is present.
         */
        std::shared_ptr<TextureView> getSwapchainTextureView();
    };

    /**
     * @brief Free-function alias for `Device::getEngineVersion()`.
     * @return Library version string in `MAJOR.MINOR.PATCH` format.
     */
    std::string getVersion();

}
