#pragma once

#include <memory>
#include <optional>
#include <vector>
#include <campello_gpu/pipeline_layout.hpp>
#include <campello_gpu/descriptors/ray_tracing_shader_descriptor.hpp>

namespace systems::leal::campello_gpu {

    /**
     * @brief Describes a single hit group within a ray tracing pipeline.
     *
     * A hit group bundles the shaders that handle intersection events for a class of
     * geometry. Triangle hit groups typically need only a `closestHit` shader;
     * procedural (AABB) hit groups also need an `intersection` shader.
     *
     * All three shader slots are optional. The `anyHit` shader is skipped when
     * omitted (equivalent to an implicit pass-through any-hit). The `intersection`
     * shader is omitted for triangle hit groups (the hardware handles intersection).
     *
     * @code
     * RayTracingHitGroupDescriptor hg{};
     * hg.closestHit = RayTracingShaderDescriptor{ shader, "closestHitMain" };
     * @endcode
     */
    struct RayTracingHitGroupDescriptor {
        /**
         * @brief Closest-hit shader invoked for the nearest accepted intersection.
         *
         * Omit to use a no-op closest-hit (ray payload is left unmodified).
         */
        std::optional<RayTracingShaderDescriptor> closestHit;

        /**
         * @brief Any-hit shader invoked for every candidate intersection.
         *
         * Omit to accept all intersections (equivalent to `opaque` geometry).
         * Use to implement alpha-tested or transparency effects.
         */
        std::optional<RayTracingShaderDescriptor> anyHit;

        /**
         * @brief Custom intersection shader for procedural (AABB) geometry.
         *
         * Must be omitted for triangle hit groups; the hardware provides triangle
         * intersection automatically. Required when the BLAS contains bounding-box
         * geometry.
         */
        std::optional<RayTracingShaderDescriptor> intersection;
    };

    /**
     * @brief Full configuration for creating a `RayTracingPipeline`.
     *
     * Specifies every shader stage in the ray tracing pipeline together with the
     * resource layout and recursion limit. Pass to `Device::createRayTracingPipeline()`.
     *
     * **Minimal single-hit-group pipeline:**
     * @code
     * RayTracingPipelineDescriptor desc{};
     * desc.rayGeneration   = { shader, "rayGenMain" };
     * desc.missShaders     = {{ shader, "missMain" }};
     * desc.hitGroups       = { hg };
     * desc.layout          = pipelineLayout;
     * desc.maxRecursionDepth = 1;
     * auto rt = device->createRayTracingPipeline(desc);
     * @endcode
     */
    struct RayTracingPipelineDescriptor {
        /**
         * @brief Ray generation shader (required).
         *
         * The entry point of every ray tracing pass. Called once per pixel (or per
         * `traceRays()` invocation) and is responsible for constructing the initial
         * ray and writing the output.
         */
        RayTracingShaderDescriptor rayGeneration;

        /**
         * @brief Miss shaders invoked when a ray finds no intersection.
         *
         * At least one miss shader is required. Multiple miss shaders are indexed
         * by the `missShaderIndex` passed to `traceRay` in the ray generation shader.
         */
        std::vector<RayTracingShaderDescriptor> missShaders;

        /**
         * @brief Hit groups indexed by geometry and instance offsets.
         *
         * Each hit group handles intersection events for one class of geometry.
         * At least one hit group is required when any geometry can be intersected.
         */
        std::vector<RayTracingHitGroupDescriptor> hitGroups;

        /**
         * @brief Pipeline layout describing the bind group structure.
         *
         * Defines the descriptor set layouts visible to all ray tracing shader stages.
         */
        std::shared_ptr<PipelineLayout> layout;

        /**
         * @brief Maximum trace recursion depth allowed by this pipeline.
         *
         * Recursion beyond this limit produces undefined behaviour (typically a GPU
         * crash or hang). Use 1 for simple primary-ray effects; higher values allow
         * recursive reflections / refractions. Hardware limits vary — query
         * `D3D12_RAYTRACING_TIER` / `VkPhysicalDeviceRayTracingPipelinePropertiesKHR`
         * for the device maximum.
         */
        uint32_t maxRecursionDepth = 1;
    };

}
