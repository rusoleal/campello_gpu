#pragma once

#include <memory>
#include <vector>
#include <campello_gpu/bind_group_layout.hpp>

namespace systems::leal::campello_gpu {

    struct MetalPipelineLayoutData {
        std::vector<std::shared_ptr<BindGroupLayout>> bindGroupLayouts;
    };

} // namespace systems::leal::campello_gpu
