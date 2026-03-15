#pragma once

#include <campello_gpu/device.hpp>
#include <campello_gpu/constants/feature.hpp>

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
        /**
         * @brief Returns the set of optional features supported by this adapter.
         *
         * Query this before creating a device to know which optional capabilities
         * (e.g. ray tracing, BC texture compression) are available.
         *
         * @return A set of `Feature` values indicating supported optional features.
         */
        std::set<Feature> getFeatures();
    };

}
