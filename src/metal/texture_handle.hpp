#pragma once

#include "common.hpp"

namespace systems::leal::campello_gpu {

    struct MetalTextureHandle {
        MTL::Texture*       texture;
        uint64_t            allocatedSize;
        MetalDeviceData*    deviceData;
    };

}
