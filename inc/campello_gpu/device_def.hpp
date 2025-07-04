#pragma once

#include <campello_gpu/device.hpp>
#include <campello_gpu/feature.hpp>

namespace systems::leal::campello_gpu {

    class DeviceDef {
        friend class Device;
    private:
        void *native;

        DeviceDef();

    public:
        std::set<Feature> getFeatures();

    };

}