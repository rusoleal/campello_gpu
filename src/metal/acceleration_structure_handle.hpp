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
    // Built with allowCompaction: each build writes the compacted size (uint32)
    // here; zeroed when the build is recorded, so 0 means "not known yet".
    MTL::Buffer *compactedSize = nullptr;
    // Created by Device::createCompactedAccelerationStructure(): copies into
    // it compact.
    bool compacted = false;

    void setInstanced(std::vector<MTL::AccelerationStructure *> structures) {
        for (auto *s : structures) s->retain();
        for (auto *s : instanced) s->release();
        instanced = std::move(structures);
    }

    /// Makes this structure (and, for a top level, its instances' structures)
    /// resident for `encoder`'s dispatches.
    /// Records the compacted-size query after a build of this structure.
    void recordCompactedSize(MTL::AccelerationStructureCommandEncoder *encoder) {
        if (!compactedSize) return;
        *static_cast<uint32_t *>(compactedSize->contents()) = 0;
        encoder->writeCompactedAccelerationStructureSize(accelerationStructure, compactedSize, 0);
    }

    void useIn(MTL::ComputeCommandEncoder *encoder) const {
        encoder->useResource(accelerationStructure, MTL::ResourceUsageRead);
        for (auto *s : instanced) encoder->useResource(s, MTL::ResourceUsageRead);
    }
};
