// Records acceleration-structure builds, refits and copies into a command
// encoder (see rt_internal.hpp for the record layout and bvh_kernels.cpp for
// the kernels).
//
// A build is one compute pass:
//   init -> gather (one dispatch per geometry, or the instances)
//        -> Morton codes -> radix sort (8 x count/scan/scatter)
//        -> hierarchy -> clear counters -> bottom-up bounds
//        [-> treelet restructuring, for preferFastTrace]
// A refit skips the Morton codes, the sort and the hierarchy: the tree keeps
// its topology and only the boxes are recomputed. WebGPU makes each dispatch's
// storage writes visible to the next, so no barriers are needed in between.

#include "rt_internal.hpp"
#include <algorithm>
#include <cmath>

namespace systems::leal::campello_gpu {

namespace {

constexpr uint64_t kParamsBlock = 256;  // minUniformBufferOffsetAlignment

constexpr uint32_t kGeomOpaque = 1;
constexpr uint32_t kGeomAabb = 2;
constexpr uint32_t kGeomIndexed = 4;
constexpr uint32_t kGeomIndex16 = 8;
constexpr uint32_t kGeomTransform = 16;

// Mirrors `struct Params` in bvh_kernels.cpp.
struct BuilderParams {
    uint32_t n, numTiles, recBase, nodeBase;
    uint32_t primBase, leafParentBase, nodeParentBase, nodeCount;
    uint32_t sBounds, sAabbs, sKeysA, sValsA;
    uint32_t sKeysB, sValsB, sHist, sCounters;
    uint32_t shift, srcIsB, kind, buildFlags;
    uint32_t geomIndex, primStart, primCount, geomFlags;
    uint32_t srcBase, srcStride, xfBase, sCost;
    uint32_t sCount, treeletMin, pad3, pad4;
};
static_assert(sizeof(BuilderParams) == 128, "BuilderParams must match the WGSL struct");

WGPUComputePipeline builderPipeline(WebGPUDeviceData* dd, RayTracingState* rt, const char* entry,
                                    bool atomicModule) {
    auto it = rt->builderPipelines.find(entry);
    if (it != rt->builderPipelines.end()) return it->second;
    WGPUComputePipelineDescriptor desc{};
    desc.label = toStringView(entry);
    desc.layout = rt->builderPipelineLayout;
    desc.compute.module = atomicModule ? rt->atomicModule : rt->plainModule;
    desc.compute.entryPoint = toStringView(entry);
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(dd->device, &desc);
    rt->builderPipelines[entry] = pipeline;
    return pipeline;
}

WGPUBuffer selectScratch(WebGPUDeviceData* dd, RayTracingState* rt, BufferHandle* user, uint64_t need) {
    if (user && user->buffer && (user->usage & WGPUBufferUsage_Storage) && user->allocatedSize >= need) {
        return user->buffer;
    }
    if (rt->scratchSize < need) {
        if (rt->scratchBuffer) wgpuBufferRelease(rt->scratchBuffer);
        WGPUBufferDescriptor desc{};
        desc.label = toStringView("campello_gpu BVH builder scratch");
        desc.size = need;
        desc.usage = WGPUBufferUsage_Storage;
        rt->scratchBuffer = wgpuDeviceCreateBuffer(dd->device, &desc);
        rt->scratchSize = rt->scratchBuffer ? need : 0;
    }
    return rt->scratchBuffer;
}

// Treelet restructuring for AccelerationStructureBuildFlag::preferFastTrace.
// Measured on an Intel UHD 630 (terrain of 128k triangles, 1M primary rays):
// one pass over subtrees of >= 16 primitives traces 11% faster than plain
// LBVH for an 11x slower build (83 vs 7 ms); three passes over every node
// gained only 4% more for a 73x slower build.
constexpr uint32_t kTreeletPasses = 1;
constexpr uint32_t kTreeletMinPrims = 16;

/** Fills the fields every kernel of a build of `dst` shares. */
BuilderParams baseParams(const AccelerationStructureHandle* dst, const AsScratchLayout& s) {
    const AsLayout& l = dst->layout;
    const uint32_t rec = recordWordOffset(dst);
    BuilderParams p{};
    p.n = l.n;
    p.numTiles = s.numTiles;
    p.recBase = rec;
    p.nodeBase = rec + l.nodeOffset;
    p.primBase = rec + l.primOffset;
    p.leafParentBase = rec + l.leafParentOffset;
    p.nodeParentBase = rec + l.nodeParentOffset;
    p.nodeCount = l.nodeCount;
    p.sBounds = s.bounds;
    p.sAabbs = s.aabbs;
    p.sKeysA = s.keysA;
    p.sValsA = s.valsA;
    p.sKeysB = s.keysB;
    p.sValsB = s.valsB;
    p.sHist = s.hist;
    p.sCounters = s.counters;
    p.sCost = s.cost;
    p.sCount = s.count;
    p.treeletMin = kTreeletMinPrims;
    p.kind = dst->tlas ? kAsKindTlas : kAsKindBlas;
    p.buildFlags = dst->buildFlags;
    return p;
}

/** Dispatches `invocations` invocations of a `wg`-wide kernel (2D past 65535 groups). */
void dispatchLinear(WGPUComputePassEncoder pass, uint64_t invocations, uint32_t wg) {
    uint64_t groups = (invocations + wg - 1) / wg;
    if (groups == 0) return;
    uint32_t x = static_cast<uint32_t>(std::min<uint64_t>(groups, 65535));
    uint32_t y = static_cast<uint32_t>((groups + x - 1) / x);
    wgpuComputePassEncoderDispatchWorkgroups(pass, x, y, 1);
}

/** Everything one build records into, released when the build is recorded. */
class BuildRecorder {
public:
    BuildRecorder(WebGPUDeviceData* dd, RayTracingState* rt, CommandEncoderHandle* encoder,
                  AccelerationStructureHandle* dst, WGPUBuffer scratch, uint64_t scratchBytes)
        : dd_(dd), rt_(rt), encoder_(encoder), dst_(dst), scratch_(scratch), scratchBytes_(scratchBytes) {}

    ~BuildRecorder() {
        for (auto bg : bindGroups_) wgpuBindGroupRelease(bg);
        for (auto b : buffers_) wgpuBufferRelease(b);
    }

    uint32_t addParams(const BuilderParams& p) {
        params_.push_back(p);
        return static_cast<uint32_t>(params_.size() - 1);
    }

    /** A transient storage buffer holding `bytes` (written by queue write before submit). */
    WGPUBuffer uploadStorage(const void* data, uint64_t bytes) {
        WGPUBufferDescriptor desc{};
        desc.size = std::max<uint64_t>((bytes + 3) & ~uint64_t(3), 16);
        desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        WGPUBuffer b = wgpuDeviceCreateBuffer(dd_->device, &desc);
        if (!b) return nullptr;
        wgpuQueueWriteBuffer(dd_->queue, b, 0, data, (bytes + 3) & ~uint64_t(3));
        buffers_.push_back(b);
        return b;
    }

    /** Creates the params buffer; call after every addParams(). */
    bool finalizeParams() {
        std::vector<uint8_t> bytes(params_.size() * kParamsBlock, 0);
        for (size_t i = 0; i < params_.size(); ++i) {
            memcpy(bytes.data() + i * kParamsBlock, &params_[i], sizeof(BuilderParams));
        }
        WGPUBufferDescriptor desc{};
        desc.size = bytes.size();
        desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        paramsBuffer_ = wgpuDeviceCreateBuffer(dd_->device, &desc);
        if (!paramsBuffer_) return false;
        // The buffer is new and used only by this build, so a queue write
        // (which lands before the encoder's command buffer runs) is safe.
        wgpuQueueWriteBuffer(dd_->queue, paramsBuffer_, 0, bytes.data(), bytes.size());
        buffers_.push_back(paramsBuffer_);
        return true;
    }

    WGPUBindGroup bindGroup(WGPUBuffer src0 = nullptr, WGPUBuffer src1 = nullptr, WGPUBuffer src2 = nullptr) {
        WGPUBindGroupEntry e[6]{};
        for (uint32_t i = 0; i < 6; ++i) {
            e[i].binding = i;
            e[i].size = WGPU_WHOLE_SIZE;
        }
        e[0].buffer = paramsBuffer_;
        e[0].size = sizeof(BuilderParams);
        e[1].buffer = dst_->allocation.page->buffer;
        e[2].buffer = scratch_;
        e[2].size = scratchBytes_;
        e[3].buffer = src0 ? src0 : rt_->dummyBuffer;
        e[4].buffer = src1 ? src1 : rt_->dummyBuffer;
        e[5].buffer = src2 ? src2 : rt_->dummyBuffer;
        WGPUBindGroupDescriptor desc{};
        desc.layout = rt_->builderLayout;
        desc.entryCount = 6;
        desc.entries = e;
        WGPUBindGroup bg = wgpuDeviceCreateBindGroup(dd_->device, &desc);
        bindGroups_.push_back(bg);
        return bg;
    }

    void begin() {
        pass_ = beginTimedComputePass(encoder_, "campello_gpu acceleration structure build", false);
    }

    void run(const char* entry, bool atomic, WGPUBindGroup bg, uint32_t block, uint64_t invocations,
             uint32_t wg) {
        wgpuComputePassEncoderSetPipeline(pass_, builderPipeline(dd_, rt_, entry, atomic));
        uint32_t offset = static_cast<uint32_t>(block * kParamsBlock);
        wgpuComputePassEncoderSetBindGroup(pass_, 0, bg, 1, &offset);
        dispatchLinear(pass_, invocations, wg);
    }

    /** One workgroup per sort tile. */
    void runTiles(const char* entry, WGPUBindGroup bg, uint32_t block, uint32_t tiles) {
        wgpuComputePassEncoderSetPipeline(pass_, builderPipeline(dd_, rt_, entry, false));
        uint32_t offset = static_cast<uint32_t>(block * kParamsBlock);
        wgpuComputePassEncoderSetBindGroup(pass_, 0, bg, 1, &offset);
        uint32_t x = std::min<uint32_t>(tiles, 65535);
        wgpuComputePassEncoderDispatchWorkgroups(pass_, x, (tiles + x - 1) / x, 1);
    }

    void end() {
        wgpuComputePassEncoderEnd(pass_);
        wgpuComputePassEncoderRelease(pass_);
        pass_ = nullptr;
        dd_->computePasses++;
    }

private:
    WebGPUDeviceData* dd_;
    RayTracingState* rt_;
    CommandEncoderHandle* encoder_;
    AccelerationStructureHandle* dst_;
    WGPUBuffer scratch_;
    uint64_t scratchBytes_;
    WGPUBuffer paramsBuffer_ = nullptr;
    WGPUComputePassEncoder pass_ = nullptr;
    std::vector<BuilderParams> params_;
    std::vector<WGPUBindGroup> bindGroups_;
    std::vector<WGPUBuffer> buffers_;
};

/**
 * The shared tail of a build: Morton codes, sort and hierarchy (unless
 * refitting), then the bottom-up bounds. Blocks 1..8 hold the sort passes.
 */

void recordTreeBuild(BuildRecorder& r, WGPUBindGroup bg, uint32_t n, const AsScratchLayout& s, bool refit,
                     bool fastTrace) {
    if (!refit) {
        r.run("morton", false, bg, 0, n, 64);
        for (uint32_t pass = 0; pass < 8; ++pass) {
            r.runTiles("sortCount", bg, 1 + pass, s.numTiles);
            r.runTiles("sortScan", bg, 1 + pass, 1);
            r.runTiles("sortScatter", bg, 1 + pass, s.numTiles);
        }
    }
    if (n == 1) {
        r.run("single", false, bg, 0, 1, 1);
        return;
    }
    if (!refit) r.run("hierarchy", false, bg, 0, n - 1, 64);
    r.run("clearCounters", false, bg, 0, n - 1, 64);
    r.run("bounds", true, bg, 0, n, 64);
    if (fastTrace && !refit && n >= 3) {
        for (uint32_t pass = 0; pass < kTreeletPasses; ++pass) {
            r.run("clearCounters", false, bg, 0, n - 1, 64);
            r.run("treelets", true, bg, 0, n, 64);
        }
    }
}

void addSortParams(BuildRecorder& r, const BuilderParams& base) {
    for (uint32_t pass = 0; pass < 8; ++pass) {
        BuilderParams p = base;
        p.shift = 4 * pass;
        p.srcIsB = pass & 1;  // eight passes end with the sorted keys in A
        r.addParams(p);
    }
}

bool isStorage(const BufferHandle* b) {
    return b && b->buffer && (b->usage & WGPUBufferUsage_Storage);
}

WGPUBuffer wgpuOf(const BufferHandle* b) {
    return b ? b->buffer : nullptr;
}

/** Validates one geometry and fills its gather params; false with a message when unusable. */
bool geometryParams(const AccelerationStructureGeometryDescriptor& g, const GeometryBuffers& buf,
                    uint32_t index, uint32_t primStart, BuilderParams& p) {
    const char* who = "[WebGPU] buildAccelerationStructure";
    p.geomIndex = index;
    p.primStart = primStart;
    p.geomFlags = g.opaque ? kGeomOpaque : 0;
    if (g.type == AccelerationStructureGeometryType::boundingBoxes) {
        uint64_t stride = g.aabbStride ? g.aabbStride : 24;
        if (!isStorage(buf.aabb) || g.aabbOffset % 4 || stride % 4 || stride < 24) {
            fprintf(stderr, "%s: geometry %u: the AABB buffer needs BufferUsage::accelerationStructureInput "
                            "and 4-byte aligned offset/stride\n", who, index);
            return false;
        }
        p.geomFlags |= kGeomAabb;
        p.primCount = g.aabbCount;
        p.srcBase = static_cast<uint32_t>(g.aabbOffset / 4);
        p.srcStride = static_cast<uint32_t>(stride / 4);
        return true;
    }
    uint64_t stride = g.vertexStride ? g.vertexStride : 12;
    if (!isStorage(buf.vertex) || g.vertexOffset % 4 || stride % 4 || stride < 12) {
        fprintf(stderr, "%s: geometry %u: the vertex buffer needs BufferUsage::accelerationStructureInput "
                        "and 4-byte aligned offset/stride\n", who, index);
        return false;
    }
    if (g.componentType != ComponentType::ctFloat) {
        fprintf(stderr, "%s: geometry %u: only float positions are supported on WebGPU\n", who, index);
        return false;
    }
    p.srcBase = static_cast<uint32_t>(g.vertexOffset / 4);
    p.srcStride = static_cast<uint32_t>(stride / 4);
    if (g.indexBuffer) {
        if (!isStorage(buf.index)) {
            fprintf(stderr, "%s: geometry %u: the index buffer needs BufferUsage::accelerationStructureInput\n",
                    who, index);
            return false;
        }
        p.geomFlags |= kGeomIndexed;
        if (g.indexFormat == IndexFormat::uint16) p.geomFlags |= kGeomIndex16;
        p.primCount = g.indexCount / 3;
    } else {
        p.primCount = g.vertexCount / 3;
    }
    if (g.transformBuffer) {
        if (!isStorage(buf.transform) || g.transformOffset % 4) {
            fprintf(stderr, "%s: geometry %u: the transform buffer needs BufferUsage::accelerationStructureInput "
                            "and a 4-byte aligned offset\n", who, index);
            return false;
        }
        p.geomFlags |= kGeomTransform;
        p.xfBase = static_cast<uint32_t>(g.transformOffset / 4);
    }
    return true;
}

/** Inverse of a 3x4 affine transform (rows); zero when singular. */
void invertAffine(const float m[3][4], float out[3][4]) {
    const float a = m[0][0], b = m[0][1], c = m[0][2];
    const float d = m[1][0], e = m[1][1], f = m[1][2];
    const float g = m[2][0], h = m[2][1], i = m[2][2];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const float det = a * A + b * B + c * C;
    for (int r = 0; r < 3; ++r) for (int k = 0; k < 4; ++k) out[r][k] = 0.f;
    if (std::fabs(det) < 1e-30f) return;
    const float inv = 1.f / det;
    float r3[3][3] = {
        { A * inv, -(b * i - c * h) * inv, (b * f - c * e) * inv },
        { B * inv, (a * i - c * g) * inv, -(a * f - c * d) * inv },
        { C * inv, -(a * h - b * g) * inv, (a * e - b * d) * inv },
    };
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) out[r][k] = r3[r][k];
        out[r][3] = -(r3[r][0] * m[0][3] + r3[r][1] * m[1][3] + r3[r][2] * m[2][3]);
    }
}

} // namespace

bool encodeBlasBuild(CommandEncoderHandle* encoder, AccelerationStructureHandle* dst,
                     const BottomLevelAccelerationStructureDescriptor& descriptor,
                     const std::vector<GeometryBuffers>& buffers, BufferHandle* userScratch, bool refit) {
    if (dst->layout.quantized) {
        fprintf(stderr, "[WebGPU] a compacted acceleration structure can't be built or updated; build the "
                        "original and compact it again\n");
        return false;
    }
    WebGPUDeviceData* dd = dst->deviceData;
    if (buffers.size() != descriptor.geometries.size()) return false;
    RayTracingState* rt = rayTracingState(dd);
    const uint32_t n = countBlasPrimitives(descriptor);
    if (n == 0 || n != dst->layout.n) {
        fprintf(stderr, "[WebGPU] buildAccelerationStructure: the descriptor describes %u primitives, the "
                        "acceleration structure was created for %u\n", n, dst->layout.n);
        return false;
    }
    const AsScratchLayout s = computeScratchLayout(n);
    WGPUBuffer scratch = selectScratch(dd, rt, userScratch, s.totalBytes);
    if (!scratch) return false;

    BuildRecorder r(dd, rt, encoder, dst, scratch, s.totalBytes);
    const BuilderParams base = baseParams(dst, s);
    r.addParams(base);        // block 0
    addSortParams(r, base);   // blocks 1..8

    struct GeometryDispatch { uint32_t block; WGPUBindGroup bg; uint32_t count; bool aabb; };
    std::vector<BuilderParams> geometry;
    uint32_t primStart = 0;
    for (uint32_t gi = 0; gi < descriptor.geometries.size(); ++gi) {
        BuilderParams p = base;
        if (!geometryParams(descriptor.geometries[gi], buffers[gi], gi, primStart, p)) return false;
        primStart += p.primCount;
        geometry.push_back(p);
    }
    std::vector<uint32_t> blocks;
    for (const auto& p : geometry) blocks.push_back(r.addParams(p));
    if (!r.finalizeParams()) return false;

    WGPUBindGroup bg = r.bindGroup();
    r.begin();
    r.run("init", false, bg, 0, 1, 1);
    for (uint32_t gi = 0; gi < geometry.size(); ++gi) {
        const auto& g = descriptor.geometries[gi];
        const bool aabb = g.type == AccelerationStructureGeometryType::boundingBoxes;
        const GeometryBuffers& b = buffers[gi];
        WGPUBindGroup gbg = aabb ? r.bindGroup(wgpuOf(b.aabb))
                                 : r.bindGroup(wgpuOf(b.vertex), wgpuOf(b.index), wgpuOf(b.transform));
        r.run(aabb ? "gatherAabbs" : "gatherTriangles", true, gbg, blocks[gi], geometry[gi].primCount, 64);
    }
    recordTreeBuild(r, bg, n, s, refit, (dst->buildFlags & 1u) != 0);
    r.end();
    dst->built = true;
    return true;
}

bool encodeTlasBuild(CommandEncoderHandle* encoder, AccelerationStructureHandle* dst,
                     const TopLevelAccelerationStructureDescriptor& descriptor,
                     const std::vector<AccelerationStructureHandle*>& blasHandles, BufferHandle* userScratch,
                     bool refit) {
    if (dst->layout.quantized) {
        fprintf(stderr, "[WebGPU] a compacted acceleration structure can't be built or updated; build the "
                        "original and compact it again\n");
        return false;
    }
    WebGPUDeviceData* dd = dst->deviceData;
    if (blasHandles.size() != descriptor.instances.size()) return false;
    RayTracingState* rt = rayTracingState(dd);
    const uint32_t n = static_cast<uint32_t>(descriptor.instances.size());
    if (n == 0 || n != dst->layout.n) {
        fprintf(stderr, "[WebGPU] buildAccelerationStructure: the descriptor has %u instances, the "
                        "acceleration structure was created for %u\n", n, dst->layout.n);
        return false;
    }

    std::vector<uint32_t> records(size_t(n) * kAsInstanceWords, 0);
    for (uint32_t i = 0; i < n; ++i) {
        const auto& inst = descriptor.instances[i];
        uint32_t* w = records.data() + size_t(i) * kAsInstanceWords;
        float inv[3][4];
        invertAffine(inst.transform, inv);
        memcpy(w, inst.transform, 12 * sizeof(float));
        memcpy(w + 12, inv, 12 * sizeof(float));
        w[24] = (inst.instanceId & 0xFFFFFFu) | (uint32_t(inst.mask) << 24);
        w[25] = (inst.hitGroupOffset & 0xFFFFFFu) | (uint32_t(inst.opaque ? 1 : 0) << 24);
        w[26] = kAsInvalid;
        if (AccelerationStructureHandle* blas = blasHandles[i]) {
            if (blas->allocation.page != dst->allocation.page) {
                fprintf(stderr, "[WebGPU] buildAccelerationStructure: instance %u's BLAS is in another "
                                "acceleration structure heap page than the TLAS (see "
                                "Device::setAccelerationStructureHeapHint())\n", i);
                return false;
            }
            w[26] = recordWordOffset(blas);
        }
    }

    const AsScratchLayout s = computeScratchLayout(n);
    WGPUBuffer scratch = selectScratch(dd, rt, userScratch, s.totalBytes);
    if (!scratch) return false;

    BuildRecorder r(dd, rt, encoder, dst, scratch, s.totalBytes);
    const BuilderParams base = baseParams(dst, s);
    r.addParams(base);
    addSortParams(r, base);
    if (!r.finalizeParams()) return false;
    WGPUBuffer staging = r.uploadStorage(records.data(), records.size() * 4);
    if (!staging) return false;

    WGPUBindGroup bg = r.bindGroup();
    WGPUBindGroup instancesBg = r.bindGroup(staging);
    r.begin();
    r.run("init", false, bg, 0, 1, 1);
    r.run("gatherInstances", true, instancesBg, 0, n, 64);
    recordTreeBuild(r, bg, n, s, refit, (dst->buildFlags & 1u) != 0);
    r.end();
    dst->built = true;
    return true;
}

bool encodeAsCopy(WGPUCommandEncoder encoder, AccelerationStructureHandle* src,
                  AccelerationStructureHandle* dst) {
    if (src->tlas != dst->tlas || src->layout.totalBytes > dst->allocation.size) {
        fprintf(stderr, "[WebGPU] copyAccelerationStructure: the destination is too small or of another kind\n");
        return false;
    }
    if (src->tlas && src->allocation.page != dst->allocation.page) {
        fprintf(stderr, "[WebGPU] copyAccelerationStructure: a TLAS can only be copied within its heap page\n");
        return false;
    }
    const uint64_t bytes = src->layout.totalBytes;
    WGPUBuffer from = src->allocation.page->buffer;
    WGPUBuffer to = dst->allocation.page->buffer;
    if (from != to) {
        wgpuCommandEncoderCopyBufferToBuffer(encoder, from, src->allocation.offset, to, dst->allocation.offset, bytes);
    } else {
        // WebGPU forbids copies within one buffer: go through a temporary one.
        WGPUBufferDescriptor desc{};
        desc.size = bytes;
        desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer tmp = wgpuDeviceCreateBuffer(dst->deviceData->device, &desc);
        if (!tmp) return false;
        wgpuCommandEncoderCopyBufferToBuffer(encoder, from, src->allocation.offset, tmp, 0, bytes);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, tmp, 0, to, dst->allocation.offset, bytes);
        wgpuBufferRelease(tmp);
    }
    dst->layout = src->layout;
    dst->built = src->built;
    dst->buildFlags = src->buildFlags;
    dst->blasInput = src->blasInput;
    dst->tlasInput = src->tlasInput;
    return true;
}

bool encodeAsCompaction(CommandEncoderHandle* encoder, AccelerationStructureHandle* src,
                        AccelerationStructureHandle* dst) {
    if (src->tlas != dst->tlas || src->layout.n != dst->layout.n || !src->built) {
        fprintf(stderr, "[WebGPU] copyAccelerationStructure: the compacted structure wasn't made from this source\n");
        return false;
    }
    if (src->layout.quantized || !dst->layout.quantized) {
        // Nothing to quantize (an updatable source, or a compacted one copied
        // again): a plain copy, keeping dst a compacted structure.
        return encodeAsCopy(encoder->encoder, src, dst);
    }
    if (src->tlas && src->allocation.page != dst->allocation.page) {
        fprintf(stderr, "[WebGPU] copyAccelerationStructure: a TLAS can only be compacted within its heap page\n");
        return false;
    }
    WebGPUDeviceData* dd = dst->deviceData;
    RayTracingState* rt = rayTracingState(dd);

    // The source record, copied into a buffer of its own: the source and the
    // destination may share a heap page, which can't be bound twice.
    WGPUBufferDescriptor tmpDesc{};
    tmpDesc.size = src->layout.totalBytes;
    tmpDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    WGPUBuffer source = wgpuDeviceCreateBuffer(dd->device, &tmpDesc);
    if (!source) return false;
    wgpuCommandEncoderCopyBufferToBuffer(encoder->encoder, src->allocation.page->buffer, src->allocation.offset,
                                         source, 0, src->layout.totalBytes);

    const AsLayout& l = dst->layout;
    const uint32_t rec = recordWordOffset(dst);
    BuilderParams p{};
    p.n = l.n;
    p.nodeCount = l.nodeCount;
    p.recBase = rec;
    p.nodeBase = rec + l.nodeOffset;
    p.primBase = rec + l.primOffset;
    p.srcBase = src->layout.nodeOffset;
    p.srcStride = src->layout.primOffset;
    p.xfBase = l.n * (dst->tlas ? kAsInstanceWords : kAsTrianglePrimWords);

    BuildRecorder r(dd, rt, encoder, dst, rt->dummyBuffer, 16);
    r.addParams(p);
    if (!r.finalizeParams()) {
        wgpuBufferRelease(source);
        return false;
    }
    // The dummy buffer is bound read-write as scratch: the read-only slots take
    // the source, so no buffer is bound both ways in a dispatch.
    WGPUBindGroup bg = r.bindGroup(source, source, source);
    r.begin();
    r.run("compactHeader", false, bg, 0, 1, 1);
    r.run("compactNodes", false, bg, 0, l.nodeCount, 64);
    r.run("compactPrims", false, bg, 0, p.xfBase, 64);
    r.end();
    wgpuBufferRelease(source);

    dst->built = true;
    dst->buildFlags = src->buildFlags;
    dst->blasInput = src->blasInput;
    dst->tlasInput = src->tlasInput;
    return true;
}

} // namespace systems::leal::campello_gpu
