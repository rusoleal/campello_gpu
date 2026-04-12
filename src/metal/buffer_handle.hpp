#pragma once

#include "common.hpp"

namespace systems::leal::campello_gpu {

    struct MetalBufferHandle {
        MTL::Buffer*        buffer;
        uint64_t            size;
        MetalDeviceData*    deviceData;
    };

}
