#pragma once

#include <vector>
#include <unordered_map>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/constants/shader_stage.hpp>

namespace systems::leal::campello_gpu {

// Shared definition of MetalBindGroupData used across multiple translation units.
// Stored in BindGroup::native to keep the entry descriptors.
struct MetalBindGroupData {
    std::vector<BindGroupEntryDescriptor> entries;
    std::unordered_map<uint32_t, ShaderStage> visibility;
};

} // namespace systems::leal::campello_gpu
