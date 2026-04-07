#pragma once
#include "Metal.hpp"

// Internal handle stored in AccelerationStructure::native.
struct MetalAccelerationStructureData {
    MTL::AccelerationStructure *accelerationStructure;
    uint64_t buildScratchSize;
    uint64_t updateScratchSize;
};
