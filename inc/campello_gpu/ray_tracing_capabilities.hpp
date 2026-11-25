#pragma once

#include <cstdint>
#include <campello_gpu/constants/ray_tracing_tier.hpp>

namespace systems::leal::campello_gpu
{

    /**
     * @brief What a device's ray tracing implementation supports.
     *
     * Query with `Adapter::getRayTracingCapabilities()` or
     * `Device::getRayTracingCapabilities()` before using any ray tracing API.
     *
     * @code
     * auto rt = device->getRayTracingCapabilities();
     * if (rt.tier == RayTracingTier::hardware) {
     *     // full-resolution ray traced effects
     * } else if (rt.tier == RayTracingTier::software) {
     *     // reduced-resolution or progressive effects
     * }
     * if (rt.pipelines && desc.maxRecursionDepth <= rt.maxRecursionDepth) {
     *     auto pipeline = device->createRayTracingPipeline(desc);
     * }
     * @endcode
     */
    struct RayTracingCapabilities
    {
        /** How ray tracing is implemented, or `none`. */
        RayTracingTier tier = RayTracingTier::none;

        /**
         * Whether `Device::createRayTracingPipeline()` and
         * `CommandEncoder::beginRayTracingPass()` work. Acceleration structures
         * and inline ray queries may be available without pipelines (a
         * `software` tier can exist before pipeline emulation does).
         */
        bool pipelines = false;

        /**
         * Largest `RayTracingPipelineDescriptor::maxRecursionDepth` that
         * `createRayTracingPipeline()` accepts; 0 when `pipelines` is false.
         * A pipeline asking for more must not be created.
         */
        uint32_t maxRecursionDepth = 0;
    };

}
