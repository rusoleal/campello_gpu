// Acceleration-structure heap pages, record layouts and the per-device
// software ray tracing state (see rt_internal.hpp).

#include "rt_internal.hpp"
#include <algorithm>

namespace systems::leal::campello_gpu {

static uint64_t alignUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

AsLayout computeAsLayout(bool tlas, uint32_t n) {
    AsLayout l;
    l.n = n;
    l.nodeCount = n > 1 ? n - 1 : 1;
    l.nodeOffset = kAsHeaderWords;
    l.primOffset = l.nodeOffset + l.nodeCount * kAsNodeWords;
    const uint32_t primWords = tlas ? kAsInstanceWords : kAsTrianglePrimWords;
    l.leafParentOffset = l.primOffset + n * primWords;
    l.nodeParentOffset = l.leafParentOffset + n;
    const uint64_t words = uint64_t(l.nodeParentOffset) + l.nodeCount;
    l.totalBytes = alignUp(words * 4, kAsRecordAlignment);
    return l;
}

AsLayout computeCompactLayout(bool tlas, uint32_t n, bool allowUpdate) {
    if (allowUpdate) return computeAsLayout(tlas, n);  // refit needs full nodes and parents
    AsLayout l;
    l.n = n;
    l.nodeCount = n > 1 ? n - 1 : 1;
    l.nodeOffset = kAsHeaderWords;
    l.primOffset = l.nodeOffset + l.nodeCount * kAsQuantizedNodeWords;
    const uint32_t primWords = tlas ? kAsInstanceWords : kAsTrianglePrimWords;
    l.leafParentOffset = kAsInvalid;
    l.nodeParentOffset = kAsInvalid;
    l.totalBytes = alignUp((uint64_t(l.primOffset) + uint64_t(n) * primWords) * 4, kAsRecordAlignment);
    l.quantized = true;
    return l;
}

AsScratchLayout computeScratchLayout(uint32_t n) {
    AsScratchLayout s;
    s.numTiles = std::max(1u, (n + 1023) / 1024);
    uint32_t w = 0;
    s.bounds = w;   w += 8;
    s.aabbs = w;    w += n * 8;
    s.keysA = w;    w += n;
    s.valsA = w;    w += n;
    s.keysB = w;    w += n;
    s.valsB = w;    w += n;
    s.hist = w;     w += 16 * s.numTiles;
    s.counters = w; w += std::max(n, 1u);
    s.cost = w;     w += std::max(n, 1u);
    s.count = w;    w += std::max(n, 1u);
    s.totalBytes = alignUp(uint64_t(w) * 4, 256);
    return s;
}

uint32_t countBlasPrimitives(const BottomLevelAccelerationStructureDescriptor& descriptor) {
    uint64_t total = 0;
    for (const auto& g : descriptor.geometries) {
        if (g.type == AccelerationStructureGeometryType::boundingBoxes) {
            total += g.aabbCount;
        } else {
            total += (g.indexBuffer ? g.indexCount : g.vertexCount) / 3;
        }
    }
    return total > 0x7FFFFFFFull ? 0 : static_cast<uint32_t>(total);
}

static uint64_t defaultPageSize(WebGPUDeviceData* deviceData, RayTracingState* rt) {
    WGPULimits limits{};
    uint64_t cap = kAsDefaultPageSize;
    if (wgpuDeviceGetLimits(deviceData->device, &limits) == WGPUStatus_Success) {
        cap = std::min<uint64_t>(limits.maxStorageBufferBindingSize, limits.maxBufferSize);
    }
    uint64_t want = rt->pageSizeHint ? rt->pageSizeHint : kAsDefaultPageSize;
    return std::min(want, cap) / kAsRecordAlignment * kAsRecordAlignment;
}

static uint64_t maxPageSize(WebGPUDeviceData* deviceData) {
    WGPULimits limits{};
    if (wgpuDeviceGetLimits(deviceData->device, &limits) != WGPUStatus_Success) return kAsDefaultPageSize;
    return std::min<uint64_t>(limits.maxStorageBufferBindingSize, limits.maxBufferSize) /
           kAsRecordAlignment * kAsRecordAlignment;
}

static AsHeapPage* createPage(WebGPUDeviceData* deviceData, uint64_t size) {
    WGPUBufferDescriptor desc{};
    desc.label = toStringView("campello_gpu acceleration structure heap");
    desc.size = size;
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(deviceData->device, &desc);
    if (!buffer) return nullptr;
    auto* page = new AsHeapPage();
    page->buffer = buffer;
    page->size = size;
    page->freeRanges.push_back({0, size});
    deviceData->accelerationStructureBytes.fetch_add(size);
    return page;
}

static bool allocateInPage(AsHeapPage* page, uint64_t bytes, AsAllocation& out) {
    for (size_t i = 0; i < page->freeRanges.size(); ++i) {
        auto& r = page->freeRanges[i];
        if (r.second < bytes) continue;
        out.page = page;
        out.offset = r.first;
        out.size = bytes;
        r.first += bytes;
        r.second -= bytes;
        if (r.second == 0) page->freeRanges.erase(page->freeRanges.begin() + i);
        page->liveRecords++;
        return true;
    }
    return false;
}

bool allocateAsRecord(WebGPUDeviceData* deviceData, uint64_t bytes, AsHeapPage* requiredPage,
                      AsAllocation& out) {
    RayTracingState* rt = rayTracingState(deviceData);
    if (!rt) return false;
    bytes = alignUp(bytes, kAsRecordAlignment);
    if (requiredPage) return allocateInPage(requiredPage, bytes, out);

    // Newest page first, so the BLASes of one scene stay together.
    for (auto it = rt->pages.rbegin(); it != rt->pages.rend(); ++it) {
        if (allocateInPage(*it, bytes, out)) return true;
    }
    uint64_t size = std::max(defaultPageSize(deviceData, rt), bytes);
    if (size > maxPageSize(deviceData)) {
        fprintf(stderr, "[WebGPU] acceleration structure of %llu bytes exceeds the largest storage "
                        "buffer binding (%llu bytes)\n",
                (unsigned long long)bytes, (unsigned long long)maxPageSize(deviceData));
        return false;
    }
    AsHeapPage* page = createPage(deviceData, size);
    if (!page) return false;
    rt->pages.push_back(page);
    return allocateInPage(page, bytes, out);
}

void freeAsRecord(WebGPUDeviceData* deviceData, AsAllocation& a) {
    if (!a.page) return;
    AsHeapPage* page = a.page;
    auto& ranges = page->freeRanges;
    auto pos = std::lower_bound(ranges.begin(), ranges.end(), std::make_pair(a.offset, uint64_t(0)));
    pos = ranges.insert(pos, {a.offset, a.size});
    // Merge with the next and the previous free range.
    if (pos + 1 != ranges.end() && pos->first + pos->second == (pos + 1)->first) {
        pos->second += (pos + 1)->second;
        ranges.erase(pos + 1);
    }
    if (pos != ranges.begin() && (pos - 1)->first + (pos - 1)->second == pos->first) {
        (pos - 1)->second += pos->second;
        ranges.erase(pos);
    }
    a.page = nullptr;
    if (--page->liveRecords == 0 && deviceData->rayTracing) {
        // Release empty pages; command buffers still using the buffer keep
        // it alive inside WebGPU.
        auto& pages = deviceData->rayTracing->pages;
        pages.erase(std::remove(pages.begin(), pages.end(), page), pages.end());
        deviceData->accelerationStructureBytes.fetch_sub(page->size);
        wgpuBufferRelease(page->buffer);
        delete page;
    }
}

AccelerationStructureHandle::~AccelerationStructureHandle() {
    if (deviceData) {
        freeAsRecord(deviceData, allocation);
        deviceData->accelerationStructureCount--;
    }
}

RayTracingState* rayTracingState(WebGPUDeviceData* deviceData) {
    if (deviceData->rayTracing) return deviceData->rayTracing;
    auto* rt = new RayTracingState();

    WGPUBindGroupLayoutEntry entries[6]{};
    for (uint32_t i = 0; i < 6; ++i) {
        entries[i].binding = i;
        entries[i].visibility = WGPUShaderStage_Compute;
    }
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = true;
    entries[0].buffer.minBindingSize = 128;
    entries[1].buffer.type = WGPUBufferBindingType_Storage;
    entries[2].buffer.type = WGPUBufferBindingType_Storage;
    entries[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[4].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[5].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    WGPUBindGroupLayoutDescriptor bglDesc{};
    bglDesc.label = toStringView("campello_gpu BVH builder");
    bglDesc.entryCount = 6;
    bglDesc.entries = entries;
    rt->builderLayout = wgpuDeviceCreateBindGroupLayout(deviceData->device, &bglDesc);

    WGPUPipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &rt->builderLayout;
    rt->builderPipelineLayout = wgpuDeviceCreatePipelineLayout(deviceData->device, &plDesc);

    rt->plainModule = createWGSLShaderModule(deviceData->device, bvhKernelsPlainSource());
    rt->atomicModule = createWGSLShaderModule(deviceData->device, bvhKernelsAtomicSource());

    WGPUBufferDescriptor dummyDesc{};
    dummyDesc.size = 16;
    dummyDesc.usage = WGPUBufferUsage_Storage;
    rt->dummyBuffer = wgpuDeviceCreateBuffer(deviceData->device, &dummyDesc);

    WGPUBindGroupLayoutEntry launch{};
    launch.binding = 0;
    launch.visibility = WGPUShaderStage_Compute;
    launch.buffer.type = WGPUBufferBindingType_Uniform;
    launch.buffer.minBindingSize = 16;
    WGPUBindGroupLayoutDescriptor launchDesc{};
    launchDesc.label = toStringView("campello_gpu ray tracing launch");
    launchDesc.entryCount = 1;
    launchDesc.entries = &launch;
    rt->launchLayout = wgpuDeviceCreateBindGroupLayout(deviceData->device, &launchDesc);

    deviceData->rayTracing = rt;
    return rt;
}

void releaseRayTracingState(WebGPUDeviceData* deviceData) {
    RayTracingState* rt = deviceData->rayTracing;
    if (!rt) return;
    for (auto* page : rt->pages) {
        wgpuBufferRelease(page->buffer);
        delete page;
    }
    for (auto& [name, pipeline] : rt->builderPipelines) wgpuComputePipelineRelease(pipeline);
    if (rt->plainModule) wgpuShaderModuleRelease(rt->plainModule);
    if (rt->atomicModule) wgpuShaderModuleRelease(rt->atomicModule);
    if (rt->builderPipelineLayout) wgpuPipelineLayoutRelease(rt->builderPipelineLayout);
    if (rt->builderLayout) wgpuBindGroupLayoutRelease(rt->builderLayout);
    if (rt->dummyBuffer) wgpuBufferRelease(rt->dummyBuffer);
    if (rt->launchLayout) wgpuBindGroupLayoutRelease(rt->launchLayout);
    if (rt->scratchBuffer) wgpuBufferRelease(rt->scratchBuffer);
    delete rt;
    deviceData->rayTracing = nullptr;
}

} // namespace systems::leal::campello_gpu
