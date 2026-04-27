#include <campello_gpu/acceleration_structure.hpp>
#include "acceleration_structure_handle.hpp"

using namespace systems::leal::campello_gpu;

AccelerationStructure::AccelerationStructure(void* pd) {
    native = pd;
}

AccelerationStructure::~AccelerationStructure() {
    auto* handle = static_cast<AccelerationStructureHandle*>(native);
    delete handle;
}

uint64_t AccelerationStructure::getBuildScratchSize() const {
    return 0;
}

uint64_t AccelerationStructure::getUpdateScratchSize() const {
    return 0;
}
