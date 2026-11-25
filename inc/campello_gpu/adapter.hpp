#pragma once

#include <campello_gpu/device.hpp>
#include <campello_gpu/constants/feature.hpp>
#include <campello_gpu/ray_tracing_capabilities.hpp>

namespace systems::leal::campello_gpu
{

    /**
     * @brief Represents a physical GPU adapter (discrete GPU, integrated GPU, etc.).
     *
     * An `Adapter` is obtained from `Device::getAdapters()` and encapsulates one physical
     * GPU available on the system. Pass it to `Device::createDevice()` to create a logical
     * device tied to that specific adapter.
     *
     * Adapters are created exclusively by the library; construct one via `Device::getAdapters()`.
     *
     * @code
     * auto adapters = Device::getAdapters();
     * auto device   = Device::createDevice(adapters[0], nullptr);
     * @endcode
     */
    class Adapter
    {
        friend class Device;

    private:
        void *native;

        Adapter();

    public:
        ~Adapter();

        /**
         * @brief Returns the set of optional features supported by this adapter.
         *
         * Query this before creating a device to know which optional capabilities
         * (e.g. ray tracing, BC texture compression) are available.
         *
         * @return A set of `Feature` values indicating supported optional features.
         */
        std::set<Feature> getFeatures();

        /**
         * @brief Reports how this adapter implements ray tracing.
         *
         * Same meaning as `Device::getRayTracingCapabilities()`; query it
         * before creating a device to choose an adapter.
         *
         * @return The adapter's ray tracing capabilities; all-default
         *         (`RayTracingTier::none`) when ray tracing is unavailable.
         */
        RayTracingCapabilities getRayTracingCapabilities();
    };

}
