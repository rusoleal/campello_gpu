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
    delete h;
}

uint64_t AccelerationStructure::getBuildScratchSize() const {
    return native ? static_cast<const AccelerationStructureHandle*>(native)->buildScratchSize : 0;
}

uint64_t AccelerationStructure::getUpdateScratchSize() const {
    return native ? static_cast<const AccelerationStructureHandle*>(native)->updateScratchSize : 0;
}
