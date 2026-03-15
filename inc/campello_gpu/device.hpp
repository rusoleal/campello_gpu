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
#include <campello_gpu/descriptors/render_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/compute_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/descriptors/sampler_descriptor.hpp>
#include <campello_gpu/descriptors/query_set_descriptor.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/storage_mode.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/feature.hpp>

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

        /**
         * @brief Submits a completed command buffer to the GPU for execution.
         *
         * The GPU will begin executing the recorded commands asynchronously. The
         * `commandBuffer` object should be released after this call.
         *
         * @param commandBuffer The command buffer obtained from `CommandEncoder::finish()`.
         */
        void submit(std::shared_ptr<CommandBuffer> commandBuffer);

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
    };

    /**
     * @brief Free-function alias for `Device::getEngineVersion()`.
     * @return Library version string in `MAJOR.MINOR.PATCH` format.
     */
    std::string getVersion();

}
