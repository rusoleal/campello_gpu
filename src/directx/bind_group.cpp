#include "common.hpp"
#include <campello_gpu/bind_group.hpp>

using namespace systems::leal::campello_gpu;

BindGroup::BindGroup(void* pd) : native(pd) {}

BindGroup::~BindGroup() {
    if (!native) return;
    auto* h = static_cast<BindGroupHandle*>(native);
    if (h->deviceData) {
        h->deviceData->freeSrvSlots(h->srvSlotIndices);
        // The shared sampler table: its slots are freed (deferred, as SRV
        // slots) once no bind group uses it.
        std::vector<UINT> samplerSlots;
        if (!h->samplerKey.empty()) {
            std::lock_guard<std::mutex> lock(h->deviceData->samplerTableMutex);
            auto it = h->deviceData->samplerTables.find(h->samplerKey);
            if (it != h->deviceData->samplerTables.end() && --it->second.refs == 0) {
                for (UINT i = 0; i < it->second.count; ++i) samplerSlots.push_back(it->second.base + i);
                h->deviceData->samplerTables.erase(it);
            }
        }
        h->deviceData->freeSamplerSlots(samplerSlots);
    }
    delete h;
}
