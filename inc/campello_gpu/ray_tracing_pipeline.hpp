#pragma once

namespace systems::leal::campello_gpu {

    class Device;
    class RayTracingPassEncoder;

    /**
     * @brief A compiled, immutable ray tracing pipeline state object.
     *
     * A `RayTracingPipeline` links the full set of ray tracing shaders — ray generation,
     * miss, and hit groups (closest-hit / any-hit / intersection) — together with a
     * `PipelineLayout` into a single pre-compiled GPU object. It also contains the
     * platform-specific Shader Binding Table (SBT) or equivalent dispatch table.
     *
     * Ray tracing pipelines are created by `Device::createRayTracingPipeline()`.
     *
     * @code
     * RayTracingPipelineDescriptor desc;
     * desc.layout           = pipelineLayout;
     * desc.rayGeneration    = { shaderModule, "rayGenMain" };
     * desc.missShaders      = {{ shaderModule, "missMain" }};
     * desc.hitGroups        = {{ shaderModule, "closestHitMain", {}, {} }};
     * desc.maxRecursionDepth = 1;
     * auto rtPipeline = device->createRayTracingPipeline(desc);
     * @endcode
     */
    class RayTracingPipeline {
        friend class Device;
        friend class RayTracingPassEncoder;
        void *native;

        RayTracingPipeline(void *data);

    public:
        ~RayTracingPipeline();
    };

}
