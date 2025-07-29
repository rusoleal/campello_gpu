#pragma once

namespace systems::leal::campello_gpu {

    enum class ShaderStage {
        vertex   = 0x01,
        fragment = 0x02,
        compute  = 0x04,
    };

}