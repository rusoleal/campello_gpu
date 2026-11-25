#define NOMINMAX
#include <d3d12.h>
#include "common.hpp"
#include <campello_gpu/acceleration_structure.hpp>

using namespace systems::leal::campello_gpu;

AccelerationStructure::AccelerationStructure(void* data) : native(data) {}

AccelerationStructure::~AccelerationStructure() {
    if (!native) return;
    auto* h = static_cast<AccelerationStructureHandle*>(native);
    if (h->resource) h->resource->Release();
    if (h->postbuildInfo) h->postbuildInfo->Release();
    if (h->postbuildReadback) {
        if (h->postbuildMapped) h->postbuildReadback->Unmap(0, nullptr);
        h->postbuildReadback->Release();
    }
    delete h;
}

uint64_t AccelerationStructure::getBuildScratchSize() const {
    return native ? static_cast<const AccelerationStructureHandle*>(native)->buildScratchSize : 0;
}

uint64_t AccelerationStructure::getUpdateScratchSize() const {
    return native ? static_cast<const AccelerationStructureHandle*>(native)->updateScratchSize : 0;
}

uint64_t AccelerationStructure::getCompactedSize() const {
    auto* h = static_cast<const AccelerationStructureHandle*>(native);
    if (!h || !h->postbuildMapped) return 0;
    return *static_cast<volatile UINT64*>(h->postbuildMapped);
}
