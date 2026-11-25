#include <campello_gpu/acceleration_structure.hpp>
#include "acceleration_structure_handle.hpp"

using namespace systems::leal::campello_gpu;

AccelerationStructure::AccelerationStructure(void* pd) {
    native = pd;
}

AccelerationStructure::~AccelerationStructure() {
    delete static_cast<AccelerationStructureHandle*>(native);
}

uint64_t AccelerationStructure::getBuildScratchSize() const {
    auto* handle = static_cast<AccelerationStructureHandle*>(native);
    return handle ? handle->buildScratchBytes : 0;
}

uint64_t AccelerationStructure::getUpdateScratchSize() const {
    auto* handle = static_cast<AccelerationStructureHandle*>(native);
    return handle ? handle->updateScratchBytes : 0;
}

uint64_t AccelerationStructure::getCompactedSize() const {
    auto* handle = static_cast<AccelerationStructureHandle*>(native);
    if (!handle || !handle->built || !(handle->buildFlags & kAsBuildFlagAllowCompaction)) return 0;
    // Software ray tracing compacts deterministically: the size is known as
    // soon as the build is recorded.
    return computeCompactLayout(handle->tlas, handle->layout.n,
                                (handle->buildFlags & kAsBuildFlagAllowUpdate) != 0).totalBytes;
}
