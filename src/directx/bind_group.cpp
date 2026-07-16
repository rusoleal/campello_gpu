#include "common.hpp"
#include <campello_gpu/bind_group.hpp>

using namespace systems::leal::campello_gpu;

BindGroup::BindGroup(void* pd) : native(pd) {}

BindGroup::~BindGroup() {
    if (!native) return;
    auto* h = static_cast<BindGroupHandle*>(native);
    if (h->deviceData) h->deviceData->freeSrvSlots(h->srvSlotIndices);
    delete h;
}
