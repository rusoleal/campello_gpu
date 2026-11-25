#include "Metal.hpp"
#include "acceleration_structure_handle.hpp"
#include <campello_gpu/acceleration_structure.hpp>

using namespace systems::leal::campello_gpu;

AccelerationStructure::AccelerationStructure(void *data) : native(data) {}

AccelerationStructure::~AccelerationStructure() {
    if (native != nullptr) {
        auto *d = static_cast<MetalAccelerationStructureData *>(native);
        if (d->accelerationStructure)
            d->accelerationStructure->release();
        d->setInstanced({});
        if (d->compactedSize) d->compactedSize->release();
        delete d;
    }
}

uint64_t AccelerationStructure::getBuildScratchSize() const {
    return static_cast<MetalAccelerationStructureData *>(native)->buildScratchSize;
}

uint64_t AccelerationStructure::getUpdateScratchSize() const {
    return static_cast<MetalAccelerationStructureData *>(native)->updateScratchSize;
}

uint64_t AccelerationStructure::getCompactedSize() const {
    auto *d = static_cast<MetalAccelerationStructureData *>(native);
    if (!d || !d->compactedSize) return 0;
    return *static_cast<const uint32_t *>(d->compactedSize->contents());
}
