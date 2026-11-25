#pragma once

// Software ray tracing on WebGPU: acceleration structures in compute shaders.
//
// Acceleration structures live in heap pages: large storage buffers,
// suballocated in 256-byte-aligned records. WebGPU has no buffer pointers or
// bindless access, so a TLAS can only reach its BLASes when they share the
// buffer the shader binds; a TLAS is therefore placed in the page of its
// BLASes, and creating one whose BLASes sit in different pages fails (raise
// the page size with Device::setAccelerationStructureHeapHint()).
//
// Record layout, in 32-bit words from the record start (all offsets in the
// header are relative to it):
//
//   header (16 words)
//     0 kind (1 = BLAS, 2 = TLAS)   1 version   2 build flags   3 n (primitives / instances)
//     4 node offset   5 primitive offset   6 node count   7 parent offset
//     8..10 root box min   12..14 root box max
//   nodes (16 words each, binary BVH with both child boxes in the parent)
//     0..2 left min, 3 left child   4..6 left max, 7 right child
//     8..10 right min              12..14 right max
//     a child with bit 31 set is a leaf; the low bits are the primitive slot
//     (BLAS) or the instance index (TLAS); 0xFFFFFFFF is "no child"
//   primitives, BLAS (12 words each)
//     0..2 v0 / box min, 3 primitive index within its geometry
//     4..6 v1 / box max, 7 geometry index
//     8..10 v2, 11 flags (bit 0 opaque, bit 1 procedural box)
//   instances, TLAS (32 words each)
//     0..11 objectToWorld rows, 12..23 worldToObject rows
//     24 instanceId | mask << 24, 25 hitGroupOffset | flags << 24 (bit 0 force opaque)
//     26 BLAS record word offset in the page
//   parents: leaf parent per primitive (n words), then node parent per node
//     (kept so updateAccelerationStructure() can refit without rebuilding)
//
// Compacted records (copyAccelerationStructure() into a structure from
// Device::createCompactedAccelerationStructure(), when the source wasn't
// built with allowUpdate) have header version 2: quantized nodes and no
// parents (parent offset 0xFFFFFFFF). A quantized node is 12 words:
//     0..2 origin (the node's box min), 3 per-axis exponents (e + 128, 8 bits
//     each: the box spans origin + [0, 255] * 2^e)
//     4 left child, 5 right child
//     6..8 the children's boxes as 8-bit offsets: left min xyz, left max xyz,
//     right min xyz, right max xyz (byte order), rounded outward

#include "../common.hpp"
#include <campello_gpu/descriptors/bottom_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/top_level_acceleration_structure_descriptor.hpp>
#include <map>
#include <string>

namespace systems::leal::campello_gpu {

constexpr uint32_t kAsKindBlas = 1;
constexpr uint32_t kAsKindTlas = 2;
constexpr uint32_t kAsHeaderWords = 16;
constexpr uint32_t kAsNodeWords = 16;
constexpr uint32_t kAsTrianglePrimWords = 12;
constexpr uint32_t kAsInstanceWords = 32;
constexpr uint32_t kAsLeafBit = 0x80000000u;
constexpr uint32_t kAsInvalid = 0xFFFFFFFFu;
constexpr uint64_t kAsRecordAlignment = 256;   // minStorageBufferOffsetAlignment
constexpr uint64_t kAsDefaultPageSize = 256ull << 20;
// Binding of the uniform holding, per bind group with acceleration
// structures, each one's record word offset (indexed by binding number).
constexpr uint32_t kAsRootsBinding = 999;

/** Where each section of a record of `n` primitives/instances sits (words). */
struct AsLayout {
    uint32_t n = 0;
    uint32_t nodeCount = 0;          // max(n - 1, 1)
    uint32_t nodeOffset = 0;
    uint32_t primOffset = 0;
    uint32_t leafParentOffset = 0;
    uint32_t nodeParentOffset = 0;
    uint64_t totalBytes = 0;         // rounded to kAsRecordAlignment
    bool quantized = false;          // compacted: version-2 nodes, no parents
};
AsLayout computeAsLayout(bool tlas, uint32_t n);
/** The layout a structure compacts to (quantized unless it must stay updatable). */
AsLayout computeCompactLayout(bool tlas, uint32_t n, bool allowUpdate);

constexpr uint32_t kAsQuantizedNodeWords = 12;
constexpr uint32_t kAsBuildFlagAllowUpdate = 0x04;
constexpr uint32_t kAsBuildFlagAllowCompaction = 0x08;

/** Scratch words the builder needs for `n` primitives/instances. */
struct AsScratchLayout {
    uint32_t numTiles = 0;
    uint32_t bounds = 0, aabbs = 0, keysA = 0, valsA = 0, keysB = 0, valsB = 0, hist = 0, counters = 0;
    uint32_t cost = 0, count = 0;  // per node, for treelet restructuring
    uint64_t totalBytes = 0;
};
AsScratchLayout computeScratchLayout(uint32_t n);

struct AsHeapPage {
    WGPUBuffer buffer = nullptr;
    uint64_t size = 0;
    std::vector<std::pair<uint64_t, uint64_t>> freeRanges;  // (offset, size), sorted
    uint32_t liveRecords = 0;
};

struct AsAllocation {
    AsHeapPage* page = nullptr;
    uint64_t offset = 0;
    uint64_t size = 0;
};

/** Per-device software ray tracing state, created on first use. */
struct RayTracingState {
    std::vector<AsHeapPage*> pages;
    uint64_t pageSizeHint = 0;  // 0 = default

    WGPUBindGroupLayout builderLayout = nullptr;
    WGPUPipelineLayout builderPipelineLayout = nullptr;
    WGPUShaderModule plainModule = nullptr;
    WGPUShaderModule atomicModule = nullptr;
    std::map<std::string, WGPUComputePipeline> builderPipelines;

    WGPUBuffer dummyBuffer = nullptr;      // bound where a builder kernel reads nothing
    WGPUBindGroupLayout launchLayout = nullptr;  // ray tracing pipelines' launch-size uniform
    WGPUBuffer scratchBuffer = nullptr;    // used when the caller's scratch can't be bound
    uint64_t scratchSize = 0;
};

RayTracingState* rayTracingState(WebGPUDeviceData* deviceData);
void releaseRayTracingState(WebGPUDeviceData* deviceData);

/**
 * Allocates a record of `bytes` in `requiredPage`, or in any page when it is
 * null (new pages are created as needed). False when it doesn't fit.
 */
bool allocateAsRecord(WebGPUDeviceData* deviceData, uint64_t bytes, AsHeapPage* requiredPage,
                      AsAllocation& out);
void freeAsRecord(WebGPUDeviceData* deviceData, AsAllocation& allocation);

struct AccelerationStructureHandle {
    WebGPUDeviceData* deviceData = nullptr;
    bool tlas = false;
    AsAllocation allocation;
    AsLayout layout;
    uint64_t buildScratchBytes = 0;
    uint64_t updateScratchBytes = 0;
    uint32_t buildFlags = 0;
    // The latest build input; updateAccelerationStructure() refits from it
    // (the API passes no descriptor to updates).
    BottomLevelAccelerationStructureDescriptor blasInput;
    TopLevelAccelerationStructureDescriptor tlasInput;
    bool built = false;      // a build has been recorded (its compacted size is known)
    bool compacted = false;  // from createCompactedAccelerationStructure(): copies into it compact

    ~AccelerationStructureHandle();
};

/** Word offset of a record in its page (what instances and roots store). */
inline uint32_t recordWordOffset(const AccelerationStructureHandle* h) {
    return static_cast<uint32_t>(h->allocation.offset / 4);
}

/** Number of primitives a BLAS descriptor describes (0 when invalid). */
uint32_t countBlasPrimitives(const BottomLevelAccelerationStructureDescriptor& descriptor);

/** The buffers of one BLAS geometry, resolved by the caller (a friend of Buffer). */
struct GeometryBuffers {
    BufferHandle* vertex = nullptr;
    BufferHandle* index = nullptr;
    BufferHandle* transform = nullptr;
    BufferHandle* aabb = nullptr;
};

/**
 * Record the build (or refit) of `dst` into `encoder`. `buffers` holds one
 * entry per geometry, `blas` one BLAS handle (or null) per instance.
 * `userScratch` is used when it is a storage buffer of sufficient size, else a
 * device-owned one. False (with a message on stderr) when the input can't be
 * built.
 */
bool encodeBlasBuild(CommandEncoderHandle* encoder, AccelerationStructureHandle* dst,
                     const BottomLevelAccelerationStructureDescriptor& descriptor,
                     const std::vector<GeometryBuffers>& buffers, BufferHandle* userScratch, bool refit);
bool encodeTlasBuild(CommandEncoderHandle* encoder, AccelerationStructureHandle* dst,
                     const TopLevelAccelerationStructureDescriptor& descriptor,
                     const std::vector<AccelerationStructureHandle*>& blas, BufferHandle* userScratch,
                     bool refit);
bool encodeAsCopy(WGPUCommandEncoder encoder, AccelerationStructureHandle* src,
                  AccelerationStructureHandle* dst);
/** Copies `src` into the compacted `dst` (quantizing its nodes when dst's layout is quantized). */
bool encodeAsCompaction(CommandEncoderHandle* encoder, AccelerationStructureHandle* src,
                        AccelerationStructureHandle* dst);

/**
 * Rewrites WGSL that declares `campello_acceleration_structure` variables
 * into plain WGSL using the traversal library (see rt_shader.cpp); returns
 * other sources unchanged.
 */
std::string rewriteRayTracingWGSL(const std::string& source);

/**
 * Whether `source` is a ray tracing pipeline module (it traces with
 * campello_traceRayPipeline, declares CampelloPayload or uses the launch
 * builtins). Such modules only compile once stitched into a pipeline, so
 * createShaderModule() keeps their source instead of compiling them.
 */
bool isRayTracingPipelineSource(const std::string& source);

/** The stage functions of a ray tracing pipeline, by name ("" = absent). */
struct RayTracingStages {
    struct HitGroup {
        std::string closestHit, anyHit, intersection;
    };
    std::string rayGeneration;
    std::vector<std::string> miss;
    std::vector<HitGroup> hitGroups;
};

/**
 * Stitches the stages of `source` into one compute shader with the entry
 * point campello_rt_main; the launch size is read from bind group
 * `launchGroup`, binding 0. Closest-hit and miss functions that trace are
 * cloned per recursion level up to `maxRecursionDepth`; a trace past it
 * returns at once. False with `error` set when a stage is missing, an any-hit
 * or intersection function traces, or a stage traces at depth 1.
 */
bool buildRayTracingPipelineWGSL(const std::string& source, const RayTracingStages& stages, uint32_t launchGroup,
                                 uint32_t maxRecursionDepth, std::string& out, std::string& error);

/**
 * Merges the top-level declarations of several modules into one source.
 * Declarations identical to an earlier module's are shared; a same-named but
 * different one is renamed in its module (`renames[k]`: old -> new name).
 */
std::string mergeRayTracingModules(const std::vector<std::string>& sources,
                                   std::vector<std::map<std::string, std::string>>& renames);

/** The deepest recursion ray tracing pipelines support on this backend (unrolled per level). */
constexpr uint32_t kSoftwareMaxRecursionDepth = 4;

const char* bvhKernelsPlainSource();
const char* bvhKernelsAtomicSource();

} // namespace systems::leal::campello_gpu
