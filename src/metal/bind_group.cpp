#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>

using namespace systems::leal::campello_gpu;

// MetalBindGroupData mirrors the definition in device.cpp; keeps stored entries for
// use by RenderPassEncoder::setBindGroup and ComputePassEncoder::setBindGroup.
struct MetalBindGroupData {
    std::vector<BindGroupEntryDescriptor> entries;
};

BindGroup::BindGroup(void *pd) : native(pd) {}

BindGroup::~BindGroup() {
    if (native != nullptr) {
        delete static_cast<MetalBindGroupData *>(native);
    }
}
