#pragma once

#include <vector>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>

namespace systems::leal::campello_gpu {

struct MetalBindGroupLayoutData {
    std::vector<EntryObject> entries;
};

} // namespace systems::leal::campello_gpu
