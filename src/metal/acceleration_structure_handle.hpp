#pragma once
#include "Metal.hpp"
#include <vector>

// Internal handle stored in AccelerationStructure::native.
struct MetalAccelerationStructureData {
    MTL::AccelerationStructure *accelerationStructure;
    uint64_t buildScratchSize;
    uint64_t updateScratchSize;
    // Top level: the bottom-level structures its instances reference
    // (retained). Metal doesn't make them resident through the top level:
    // passes that bind it must useResource() each one, or rays miss them
    // once the system pages them out.
    std::vector<MTL::AccelerationStructure *> instanced;

    void setInstanced(std::vector<MTL::AccelerationStructure *> structures) {
        for (auto *s : structures) s->retain();
        for (auto *s : instanced) s->release();
        instanced = std::move(structures);
    }

    /// Makes this structure (and, for a top level, its instances' structures)
    /// resident for `encoder`'s dispatches.
    void useIn(MTL::ComputeCommandEncoder *encoder) const {
        encoder->useResource(accelerationStructure, MTL::ResourceUsageRead);
        for (auto *s : instanced) encoder->useResource(s, MTL::ResourceUsageRead);
    }
};
