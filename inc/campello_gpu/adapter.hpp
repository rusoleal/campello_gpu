#pragma once

#include <campello_gpu/device.hpp>
#include <campello_gpu/constants/feature.hpp>

namespace systems::leal::campello_gpu
{

    class Adapter
    {
        friend class Device;

    private:
        void *native;

        Adapter();

    public:
        std::set<Feature> getFeatures();
    };

}