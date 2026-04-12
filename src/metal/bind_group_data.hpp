#pragma once

#include <vector>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>

namespace systems::leal::campello_gpu {

// Shared definition of MetalBindGroupData used across multiple translation units.
// Stored in BindGroup::native to keep the entry descriptors.
struct MetalBindGroupData {
    std::vector<BindGroupEntryDescriptor> entries;
};

} // namespace systems::leal::campello_gpu
