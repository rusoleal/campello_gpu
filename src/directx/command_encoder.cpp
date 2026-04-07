#define NOMINMAX
#include "common.hpp"
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <campello_gpu/descriptors/bottom_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/top_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/constants/index_format.hpp>
#include <campello_gpu/constants/acceleration_structure_build_flag.hpp>
#include <campello_gpu/constants/acceleration_structure_geometry_type.hpp>
#include <cstring>

// Inline helper for calculating subresource index (equivalent to D3D12CalcSubresource)
inline UINT CalcSubresource(UINT mipSlice, UINT arraySlice, UINT planeSlice,
                            UINT mipLevels, UINT arraySize) {
    return mipSlice + arraySlice * mipLevels + planeSlice * mipLevels * arraySize;
}

using namespace systems::leal::campello_gpu;

CommandEncoder::CommandEncoder(void* pd) : native(pd) {}

CommandEncoder::~CommandEncoder() {
    if (!native) return;
    auto* h = static_cast<CommandEncoderHandle*>(native);
    // If finish() was not called the list is still open — close it.
    if (h->cmdList)   { h->cmdList->Close(); h->cmdList->Release(); }
    if (h->allocator) h->allocator->Release();
    delete h;
}

std::shared_ptr<ComputePassEncoder> CommandEncoder::beginComputePass() {
    if (!native) return nullptr;
    auto* h  = static_cast<CommandEncoderHandle*>(native);

    auto* ph          = new ComputePassEncoderHandle();
    ph->cmdList       = h->cmdList;
    ph->deviceData    = h->deviceData;
    return std::shared_ptr<ComputePassEncoder>(new ComputePassEncoder(ph));
}

std::shared_ptr<RenderPassEncoder> CommandEncoder::beginRenderPass(
    const BeginRenderPassDescriptor& descriptor)
{
    if (!native) return nullptr;
    auto* h    = static_cast<CommandEncoderHandle*>(native);
    auto* list = h->cmdList;

    // Set up render target transition + OMSetRenderTargets
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    bool hasDSV = false;

    for (const auto& ca : descriptor.colorAttachments) {
        if (!ca.view || !ca.view->native) continue;
        auto* tvh = static_cast<TextureViewHandle*>(ca.view->native);
        rtvHandles.push_back(tvh->cpuHandle);

        if (ca.loadOp == LoadOp::clear) {
            list->ClearRenderTargetView(tvh->cpuHandle, ca.clearValue, 0, nullptr);
        }
    }

    if (descriptor.depthStencilAttachment) {
        const auto& dsa = *descriptor.depthStencilAttachment;
        if (dsa.view && dsa.view->native) {
            auto* tvh = static_cast<TextureViewHandle*>(dsa.view->native);
            dsvHandle = tvh->cpuHandle;
            hasDSV    = true;

            D3D12_CLEAR_FLAGS flags = {};
            if (dsa.depthLoadOp == LoadOp::clear)   flags |= D3D12_CLEAR_FLAG_DEPTH;
            if (dsa.stencilLoadOp == LoadOp::clear) flags |= D3D12_CLEAR_FLAG_STENCIL;
            if (flags)
                list->ClearDepthStencilView(dsvHandle, flags,
                    dsa.depthClearValue,
                    static_cast<UINT8>(dsa.stencilClearValue), 0, nullptr);
        }
    }

    list->OMSetRenderTargets(
        static_cast<UINT>(rtvHandles.size()),
        rtvHandles.empty() ? nullptr : rtvHandles.data(),
        FALSE,
        hasDSV ? &dsvHandle : nullptr);

    auto* ph        = new RenderPassEncoderHandle();
    ph->cmdList     = list;
    ph->deviceData  = h->deviceData;
    if (descriptor.occlusionQuerySet && descriptor.occlusionQuerySet->native) {
        auto* qsh      = static_cast<QuerySetHandle*>(descriptor.occlusionQuerySet->native);
        ph->queryHeap  = qsh->queryHeap;
        ph->queryType  = qsh->queryType;
    }
    return std::shared_ptr<RenderPassEncoder>(new RenderPassEncoder(ph));
}

void CommandEncoder::clearBuffer(std::shared_ptr<Buffer> buffer,
                                 uint64_t offset, uint64_t size) {
    if (!native || !buffer || !buffer->native) return;
    auto* h   = static_cast<CommandEncoderHandle*>(native);
    auto* bh  = static_cast<BufferHandle*>(buffer->native);
    // Zero-fill via memset on the persistently-mapped upload buffer.
    if (bh->mapped)
        std::memset(static_cast<uint8_t*>(bh->mapped) + offset, 0,
                    std::min(size, bh->size - offset));
}

void CommandEncoder::copyBufferToBuffer(
    std::shared_ptr<Buffer> source,    uint64_t sourceOffset,
    std::shared_ptr<Buffer> destination, uint64_t destinationOffset,
    uint64_t size)
{
    if (!native || !source || !destination) return;
    auto* h    = static_cast<CommandEncoderHandle*>(native);
    auto* src  = static_cast<BufferHandle*>(source->native);
    auto* dst  = static_cast<BufferHandle*>(destination->native);
    h->cmdList->CopyBufferRegion(dst->resource, destinationOffset,
                                  src->resource, sourceOffset, size);
}

void CommandEncoder::copyBufferToTexture(
    std::shared_ptr<Buffer>  source,
    uint64_t                 sourceOffset,
    uint64_t                 bytesPerRow,
    std::shared_ptr<Texture> destination,
    uint32_t                 mipLevel,
    uint32_t                 arrayLayer)
{
    if (!native || !source || !destination || !source->native || !destination->native) return;
    auto* h   = static_cast<CommandEncoderHandle*>(native);
    auto* src = static_cast<BufferHandle*>(source->native);
    auto* dst = static_cast<TextureHandle*>(destination->native);

    D3D12_RESOURCE_DESC texDesc = dst->resource->GetDesc();

    UINT subresource = CalcSubresource(mipLevel, arrayLayer, 0,
        texDesc.MipLevels, texDesc.DepthOrArraySize);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT   numRows;
    UINT64 rowSizeInBytes;
    UINT64 totalBytes;
    h->deviceData->device->GetCopyableFootprints(&texDesc, subresource, 1, sourceOffset,
        &footprint, &numRows, &rowSizeInBytes, &totalBytes);

    if (bytesPerRow > 0)
        footprint.Footprint.RowPitch = static_cast<UINT>(bytesPerRow);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = dst->resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = subresource;
    h->cmdList->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource       = src->resource;
    srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource        = dst->resource;
    dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = subresource;

    h->cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
    h->cmdList->ResourceBarrier(1, &barrier);
}

void CommandEncoder::copyTextureToBuffer(
    std::shared_ptr<Texture> source,
    uint32_t mipLevel,
    uint32_t arrayLayer,
    std::shared_ptr<Buffer> destination,
    uint64_t destinationOffset,
    uint64_t bytesPerRow)
{
    if (!native || !source || !destination || !source->native || !destination->native) return;
    auto* h   = static_cast<CommandEncoderHandle*>(native);
    auto* src = static_cast<TextureHandle*>(source->native);
    auto* dst = static_cast<BufferHandle*>(destination->native);

    // Get the texture description to calculate the footprint
    D3D12_RESOURCE_DESC texDesc = src->resource->GetDesc();

    // Calculate the subresource index
    UINT subresource = CalcSubresource(mipLevel, arrayLayer, 0, 
        texDesc.MipLevels, texDesc.DepthOrArraySize);

    // Get copyable footprint
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT numRows;
    UINT64 rowSizeInBytes;
    UINT64 totalBytes;
    h->deviceData->device->GetCopyableFootprints(&texDesc, subresource, 1, 0,
        &footprint, &numRows, &rowSizeInBytes, &totalBytes);

    // Transition texture to COPY_SOURCE state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = src->resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = subresource;
    h->cmdList->ResourceBarrier(1, &barrier);

    // Setup copy locations
    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource        = src->resource;
    srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = subresource;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource       = dst->resource;
    dstLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint = footprint;
    // Override the footprint offset to account for destinationOffset
    dstLoc.PlacedFootprint.Offset = destinationOffset;

    // Copy the texture region
    h->cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    // Transition texture back to COMMON state
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
    h->cmdList->ResourceBarrier(1, &barrier);
}

void CommandEncoder::copyTextureToTexture(
    std::shared_ptr<Texture> /*source*/,
    const Offset3D& /*sourceOffset*/,
    std::shared_ptr<Texture> /*destination*/,
    const Offset3D& /*destinationOffset*/,
    const Extent3D& /*extent*/)
{
    // TODO: Implement using ID3D12GraphicsCommandList::CopyTextureRegion
}

std::shared_ptr<CommandBuffer> CommandEncoder::finish() {
    if (!native) return nullptr;
    auto* h = static_cast<CommandEncoderHandle*>(native);

    // Transition swapchain back buffer from RENDER_TARGET back to PRESENT
    if (h->deviceData->swapChain) {
        UINT frameIdx = h->deviceData->frameIndex;
        ID3D12Resource* rt = h->deviceData->renderTargets[frameIdx];
        if (rt) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = rt;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            h->cmdList->ResourceBarrier(1, &barrier);
        }
    }

    if (FAILED(h->cmdList->Close())) return nullptr;

    auto* cbh               = new CommandBufferHandle();
    cbh->cmdList            = h->cmdList;
    cbh->allocator          = h->allocator;
    cbh->deviceData         = h->deviceData;
    cbh->stagingResources   = std::move(h->stagingResources);

    // Ownership transferred to CommandBufferHandle
    h->cmdList   = nullptr;
    h->allocator = nullptr;

    return std::shared_ptr<CommandBuffer>(new CommandBuffer(cbh));
}

void CommandEncoder::resolveQuerySet(
    std::shared_ptr<QuerySet> querySet,
    uint32_t firstQuery, uint32_t queryCount,
    std::shared_ptr<Buffer> destination, uint64_t destinationOffset)
{
    if (!native || !querySet) return;
    auto* h   = static_cast<CommandEncoderHandle*>(native);
    auto* qsh = static_cast<QuerySetHandle*>(querySet->native);
    // Resolve into the QuerySet's own readback buffer (D3D12_HEAP_TYPE_READBACK,
    // already in COPY_DEST state). Upload-heap buffers cannot serve as COPY_DEST.
    if (qsh->resultBuffer)
        h->cmdList->ResolveQueryData(qsh->queryHeap, qsh->queryType,
                                      firstQuery, queryCount,
                                      qsh->resultBuffer, destinationOffset);
}

void CommandEncoder::writeTimestamp(
    std::shared_ptr<QuerySet> querySet, uint32_t queryIndex)
{
    if (!native || !querySet) return;
    auto* h   = static_cast<CommandEncoderHandle*>(native);
    auto* qsh = static_cast<QuerySetHandle*>(querySet->native);
    h->cmdList->EndQuery(qsh->queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
}

// -----------------------------------------------------------------------
// Ray tracing commands
// -----------------------------------------------------------------------

// Local geometry-desc builder (mirrors the one in device.cpp)
static std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> buildGeometryDescs_CE(
    const BottomLevelAccelerationStructureDescriptor& descriptor)
{
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> descs;
    descs.reserve(descriptor.geometries.size());
    for (const auto& geo : descriptor.geometries) {
        D3D12_RAYTRACING_GEOMETRY_DESC gd = {};
        gd.Flags = geo.opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
                              : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        if (geo.type == AccelerationStructureGeometryType::triangles) {
            gd.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            auto& tri = gd.Triangles;
            if (geo.vertexBuffer && geo.vertexBuffer->native) {
                auto* bh = static_cast<BufferHandle*>(geo.vertexBuffer->native);
                tri.VertexBuffer.StartAddress  = bh->resource->GetGPUVirtualAddress() + geo.vertexOffset;
                tri.VertexBuffer.StrideInBytes = geo.vertexStride;
                tri.VertexCount                = geo.vertexCount;
                tri.VertexFormat               = DXGI_FORMAT_R32G32B32_FLOAT;
            }
            if (geo.indexBuffer && geo.indexBuffer->native && geo.indexCount > 0) {
                auto* ib = static_cast<BufferHandle*>(geo.indexBuffer->native);
                tri.IndexBuffer = ib->resource->GetGPUVirtualAddress();
                tri.IndexCount  = geo.indexCount;
                tri.IndexFormat = (geo.indexFormat == IndexFormat::uint16)
                    ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
            }
            if (geo.transformBuffer && geo.transformBuffer->native) {
                auto* tb = static_cast<BufferHandle*>(geo.transformBuffer->native);
                tri.Transform3x4 = tb->resource->GetGPUVirtualAddress() + geo.transformOffset;
            }
        } else {
            gd.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
            auto& aabb = gd.AABBs;
            if (geo.aabbBuffer && geo.aabbBuffer->native) {
                auto* ab = static_cast<BufferHandle*>(geo.aabbBuffer->native);
                aabb.AABBs.StartAddress  = ab->resource->GetGPUVirtualAddress() + geo.aabbOffset;
                aabb.AABBs.StrideInBytes = geo.aabbStride;
                aabb.AABBCount           = geo.aabbCount;
            }
        }
        descs.push_back(gd);
    }
    return descs;
}

static D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS mapBuildFlags_CE(
    AccelerationStructureBuildFlag f)
{
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS out =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    int bf = static_cast<int>(f);
    if (bf & static_cast<int>(AccelerationStructureBuildFlag::preferFastTrace))
        out |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    if (bf & static_cast<int>(AccelerationStructureBuildFlag::preferFastBuild))
        out |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    if (bf & static_cast<int>(AccelerationStructureBuildFlag::allowUpdate))
        out |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    if (bf & static_cast<int>(AccelerationStructureBuildFlag::allowCompaction))
        out |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
    return out;
}

std::shared_ptr<RayTracingPassEncoder> CommandEncoder::beginRayTracingPass() {
    if (!native) return nullptr;
    auto* h = static_cast<CommandEncoderHandle*>(native);

    ID3D12GraphicsCommandList4* list4 = nullptr;
    if (FAILED(h->cmdList->QueryInterface(IID_PPV_ARGS(&list4)))) return nullptr;

    auto* ph        = new RayTracingPassEncoderHandle();
    ph->cmdList4    = list4;  // ownership transferred; released in ~RayTracingPassEncoderHandle
    ph->deviceData  = h->deviceData;
    return std::shared_ptr<RayTracingPassEncoder>(new RayTracingPassEncoder(ph));
}

void CommandEncoder::buildAccelerationStructure(
    std::shared_ptr<AccelerationStructure> dst,
    const BottomLevelAccelerationStructureDescriptor& descriptor,
    std::shared_ptr<Buffer> scratchBuffer)
{
    if (!native || !dst || !dst->native || !scratchBuffer || !scratchBuffer->native) return;
    auto* h   = static_cast<CommandEncoderHandle*>(native);
    auto* ash = static_cast<AccelerationStructureHandle*>(dst->native);
    auto* sbh = static_cast<BufferHandle*>(scratchBuffer->native);

    ID3D12GraphicsCommandList4* list4 = nullptr;
    if (FAILED(h->cmdList->QueryInterface(IID_PPV_ARGS(&list4)))) return;

    auto geomDescs = buildGeometryDescs_CE(descriptor);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.DestAccelerationStructureData    = ash->gpuVA;
    buildDesc.ScratchAccelerationStructureData = sbh->resource->GetGPUVirtualAddress();
    buildDesc.Inputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    buildDesc.Inputs.Flags          = mapBuildFlags_CE(descriptor.buildFlags);
    buildDesc.Inputs.NumDescs       = static_cast<UINT>(geomDescs.size());
    buildDesc.Inputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
    buildDesc.Inputs.pGeometryDescs = geomDescs.empty() ? nullptr : geomDescs.data();

    list4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = ash->resource;
    list4->ResourceBarrier(1, &barrier);

    list4->Release();
}

void CommandEncoder::buildAccelerationStructure(
    std::shared_ptr<AccelerationStructure> dst,
    const TopLevelAccelerationStructureDescriptor& descriptor,
    std::shared_ptr<Buffer> scratchBuffer)
{
    if (!native || !dst || !dst->native || !scratchBuffer || !scratchBuffer->native) return;
    auto* h   = static_cast<CommandEncoderHandle*>(native);
    auto* ash = static_cast<AccelerationStructureHandle*>(dst->native);
    auto* sbh = static_cast<BufferHandle*>(scratchBuffer->native);

    ID3D12GraphicsCommandList4* list4 = nullptr;
    if (FAILED(h->cmdList->QueryInterface(IID_PPV_ARGS(&list4)))) return;

    // Upload instance descs to an upload-heap buffer
    UINT64 instBufSize = std::max<UINT64>(
        1, descriptor.instances.size()) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);

    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC   rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = instBufSize;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* instBuf = nullptr;
    if (FAILED(h->deviceData->device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&instBuf)))) {
        list4->Release(); return;
    }

    D3D12_RANGE readRange = {0, 0};
    D3D12_RAYTRACING_INSTANCE_DESC* mapped = nullptr;
    instBuf->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
    if (mapped) {
        for (size_t i = 0; i < descriptor.instances.size(); ++i) {
            const auto& inst   = descriptor.instances[i];
            auto&       d3dInst = mapped[i];
            std::memcpy(d3dInst.Transform, inst.transform, sizeof(inst.transform));
            d3dInst.InstanceID                          = inst.instanceId  & 0xFFFFFF;
            d3dInst.InstanceMask                        = inst.mask;
            d3dInst.InstanceContributionToHitGroupIndex = inst.hitGroupOffset & 0xFFFFFF;
            d3dInst.Flags = inst.opaque
                ? D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE
                : D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            if (inst.blas && inst.blas->native) {
                auto* blasH = static_cast<AccelerationStructureHandle*>(inst.blas->native);
                d3dInst.AccelerationStructure = blasH->gpuVA;
            }
        }
        instBuf->Unmap(0, nullptr);
    }
    // Keep alive until GPU completes
    h->stagingResources.push_back(instBuf);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.DestAccelerationStructureData    = ash->gpuVA;
    buildDesc.ScratchAccelerationStructureData = sbh->resource->GetGPUVirtualAddress();
    buildDesc.Inputs.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    buildDesc.Inputs.Flags         = mapBuildFlags_CE(descriptor.buildFlags);
    buildDesc.Inputs.NumDescs      = static_cast<UINT>(descriptor.instances.size());
    buildDesc.Inputs.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;
    buildDesc.Inputs.InstanceDescs = instBuf->GetGPUVirtualAddress();

    list4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = ash->resource;
    list4->ResourceBarrier(1, &barrier);

    list4->Release();
}

void CommandEncoder::updateAccelerationStructure(
    std::shared_ptr<AccelerationStructure> src,
    std::shared_ptr<AccelerationStructure> dst,
    std::shared_ptr<Buffer> scratchBuffer)
{
    if (!native || !src || !src->native || !dst || !dst->native ||
        !scratchBuffer || !scratchBuffer->native) return;
    auto* h    = static_cast<CommandEncoderHandle*>(native);
    auto* srch = static_cast<AccelerationStructureHandle*>(src->native);
    auto* dsth = static_cast<AccelerationStructureHandle*>(dst->native);
    auto* sbh  = static_cast<BufferHandle*>(scratchBuffer->native);

    ID3D12GraphicsCommandList4* list4 = nullptr;
    if (FAILED(h->cmdList->QueryInterface(IID_PPV_ARGS(&list4)))) return;

    // Re-use the same type as src. For update, Inputs.Type should match the original build.
    // We use a minimal inputs struct — the driver reuses the existing BVH structure.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.SourceAccelerationStructureData  = srch->gpuVA;
    buildDesc.DestAccelerationStructureData    = dsth->gpuVA;
    buildDesc.ScratchAccelerationStructureData = sbh->resource->GetGPUVirtualAddress();
    buildDesc.Inputs.Type  = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    buildDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    // NumDescs/pGeometryDescs left at 0 — driver uses the existing structure's geometry.

    list4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = dsth->resource;
    list4->ResourceBarrier(1, &barrier);

    list4->Release();
}

void CommandEncoder::copyAccelerationStructure(
    std::shared_ptr<AccelerationStructure> src,
    std::shared_ptr<AccelerationStructure> dst)
{
    if (!native || !src || !src->native || !dst || !dst->native) return;
    auto* h    = static_cast<CommandEncoderHandle*>(native);
    auto* srch = static_cast<AccelerationStructureHandle*>(src->native);
    auto* dsth = static_cast<AccelerationStructureHandle*>(dst->native);

    ID3D12GraphicsCommandList4* list4 = nullptr;
    if (FAILED(h->cmdList->QueryInterface(IID_PPV_ARGS(&list4)))) return;

    list4->CopyRaytracingAccelerationStructure(
        dsth->gpuVA, srch->gpuVA,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);

    list4->Release();
}
