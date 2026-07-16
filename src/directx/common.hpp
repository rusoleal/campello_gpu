#pragma once

#define NOMINMAX
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <vector>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/metrics.hpp>

namespace systems::leal::campello_gpu {

// -----------------------------------------------------------------------
// Central device state
// -----------------------------------------------------------------------
struct DeviceData {
    IDXGIAdapter1*          adapter           = nullptr;
    ID3D12Device*           device            = nullptr;
    ID3D12CommandQueue*     queue             = nullptr;

    // Swapchain (only when a window handle is provided at creation)
    HWND                    hwnd              = nullptr;
    IDXGISwapChain3*        swapChain         = nullptr;
    static constexpr UINT   kFrameCount       = 2;
    ID3D12Resource*         renderTargets[kFrameCount] = {};
    ID3D12DescriptorHeap*   rtvHeap           = nullptr;   // swapchain RTVs
    UINT                    rtvDescSize       = 0;
    UINT                    frameIndex        = 0;

    // Persistent shader-visible CBV/SRV/UAV heap (textures, buffers)
    ID3D12DescriptorHeap*   srvHeap           = nullptr;
    UINT                    srvDescSize       = 0;
    UINT                    srvOffset         = 0;  // next free slot index

    // Non-shader-visible staging heap for each Texture's canonical/pre-baked
    // SRV (TextureHandle::srvCpuHandle). D3D12 forbids using a shader-visible
    // heap's CPU descriptor handle as a CopyDescriptorsSimple source (its
    // memory is write-combined/CPU-write-only on most hardware) — every
    // texture's SRV must live here and get copied INTO srvHeap per-bind-group,
    // never the other way around. Confirmed via the D3D12 debug layer:
    // "SrcDescriptorRangeStart points to a descriptor heap type that is CPU
    // write only, so reading it (in this case a copy source) is invalid."
    ID3D12DescriptorHeap*   srvStagingHeap    = nullptr;
    UINT                    srvStagingDescSize = 0;
    UINT                    srvStagingOffset  = 0;

    // Persistent shader-visible sampler heap
    ID3D12DescriptorHeap*   samplerHeap       = nullptr;
    UINT                    samplerDescSize   = 0;
    UINT                    samplerOffset     = 0;

    // CPU-only RTV heap for user render targets (non-swapchain)
    ID3D12DescriptorHeap*   rtvExtraHeap      = nullptr;
    UINT                    rtvExtraDescSize  = 0;
    UINT                    rtvExtraOffset    = 0;

    // CPU-only DSV heap
    ID3D12DescriptorHeap*   dsvHeap           = nullptr;
    UINT                    dsvDescSize       = 0;
    UINT                    dsvOffset         = 0;

    // GPU sync
    ID3D12Fence*            fence             = nullptr;
    UINT64                  fenceValue        = 0;
    HANDLE                  fenceEvent        = nullptr;

    // Debug-layer message queue (classic ID3D12InfoQueue — not ID3D12InfoQueue1,
    // whose RegisterMessageCallback isn't supported on all driver stacks, e.g.
    // older Intel iGPU drivers return E_NOINTERFACE for it). Polled once per
    // submit() in _DEBUG builds; see drainDebugMessages() in device.cpp.
    ID3D12InfoQueue*        infoQueue         = nullptr;

    // srvHeap slot reclamation. Every BindGroup::~BindGroup() used to leak
    // its slot(s) forever (srvOffset only ever incremented) — fine for
    // long-lived bind groups, fatal for anything that creates them
    // per-frame for genuinely-new resources that can't be identity-cached
    // (e.g. a widget that recreates its source texture every frame, or an
    // offscreen capture whose size changes every frame so pooling by size
    // never converges). BindGroup::~BindGroup() pushes its slots onto
    // srvPendingFreeSlots (NOT srvFreeSlots directly — the command list
    // that referenced them may not have executed yet, since GPU descriptor
    // tables are read by the GPU at *execution* time, not at recording
    // time, so reusing a slot within the same still-being-recorded frame
    // could corrupt an earlier draw call in that same frame). Device::submit()
    // merges pending into the real free list only after waitForGpu(), by
    // which point every draw call that could have referenced the old
    // contents has definitely finished executing.
    //
    // Guarded by srvMutex: campello_widgets' image loading pipeline runs a
    // background thread pool (ImageLoader) whose tasks own shared_ptr<Texture>/
    // shared_ptr<BindGroup> references — the LAST reference to one can be
    // dropped on a worker thread (e.g. a superseded/discarded load task),
    // triggering ~Texture()/~BindGroup() concurrently with the main render
    // thread's own allocations. Without a lock, two threads simultaneously
    // push_back()/pop_back() on the same std::vector is a data race that can
    // corrupt the vector's internal state — indistinguishable, from the
    // outside, from the exact kind of heap-corruption crash this was found
    // debugging (reproducible but with wildly inconsistent timing, which is
    // the signature of a race rather than a deterministic resource leak).
    std::mutex              srvMutex;
    std::vector<UINT>      srvFreeSlots;
    std::vector<UINT>      srvPendingFreeSlots;

    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuAt(UINT idx) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h = srvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += idx * srvDescSize;
        return h;
    }

    // Returns a slot index — reused from srvFreeSlots if one is available,
    // otherwise a fresh one from the end of the heap. Thread-safe — see
    // srvMutex's doc comment above.
    UINT allocSrvIndex() {
        std::lock_guard<std::mutex> lock(srvMutex);
        if (!srvFreeSlots.empty()) {
            UINT idx = srvFreeSlots.back();
            srvFreeSlots.pop_back();
            return idx;
        }
        return srvOffset++;
    }

    // Thread-safe — see srvMutex's doc comment above.
    void freeSrvSlots(const std::vector<UINT>& indices) {
        if (indices.empty()) return;
        std::lock_guard<std::mutex> lock(srvMutex);
        srvPendingFreeSlots.insert(srvPendingFreeSlots.end(), indices.begin(), indices.end());
    }

    // Thread-safe — see srvMutex's doc comment above. Called once per
    // Device::submit() after waitForGpu().
    void recycleSrvSlots() {
        std::lock_guard<std::mutex> lock(srvMutex);
        if (srvPendingFreeSlots.empty()) return;
        srvFreeSlots.insert(srvFreeSlots.end(), srvPendingFreeSlots.begin(), srvPendingFreeSlots.end());
        srvPendingFreeSlots.clear();
    }

    // Allocate one SRV slot; returns the CPU handle for it.
    D3D12_CPU_DESCRIPTOR_HANDLE allocSrvCpu() {
        return srvCpuAt(allocSrvIndex());
    }
    // Allocate one slot in the non-shader-visible staging heap — see
    // srvStagingHeap's doc comment above.
    D3D12_CPU_DESCRIPTOR_HANDLE allocSrvStagingCpu() {
        D3D12_CPU_DESCRIPTOR_HANDLE h = srvStagingHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += srvStagingOffset * srvStagingDescSize;
        ++srvStagingOffset;
        return h;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuAt(UINT idx) const {
        D3D12_GPU_DESCRIPTOR_HANDLE h = srvHeap->GetGPUDescriptorHandleForHeapStart();
        h.ptr += idx * srvDescSize;
        return h;
    }
    UINT srvCurrentIdx() const { return srvOffset == 0 ? 0 : srvOffset - 1; }

    D3D12_CPU_DESCRIPTOR_HANDLE allocSamplerCpu() {
        D3D12_CPU_DESCRIPTOR_HANDLE h = samplerHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += samplerOffset * samplerDescSize;
        ++samplerOffset;
        return h;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE samplerGpuAt(UINT idx) const {
        D3D12_GPU_DESCRIPTOR_HANDLE h = samplerHeap->GetGPUDescriptorHandleForHeapStart();
        h.ptr += idx * samplerDescSize;
        return h;
    }
    UINT samplerCurrentIdx() const { return samplerOffset == 0 ? 0 : samplerOffset - 1; }

    // rtvExtraHeap slot reclamation — same rationale and lifecycle as
    // srvFreeSlots/srvPendingFreeSlots above (see that doc comment for the
    // full explanation of the deferred-free-until-after-waitForGpu()
    // pattern). Before this, allocRtvExtraIndex() only ever incremented
    // rtvExtraOffset — fatal for rtvExtraHeap's mere 64 slots given
    // campello_widgets' D3DDrawBackend::OffscreenTexturePool creates a new
    // render-target Texture (permanently consuming one slot each) for every
    // distinct ClipRRect/ClipOval/ShaderMask/BackdropFilter offscreen size
    // encountered across a session — confirmed via a full minidump showing
    // D3D12SDKLayers!ReportCorruption raised from inside
    // Device::createTexture()'s CreateRenderTargetView call, reached via
    // OffscreenTexturePool::acquire() -> Renderer::applyClipShape().
    std::mutex        rtvExtraMutex;
    std::vector<UINT> rtvExtraFreeSlots;
    std::vector<UINT> rtvExtraPendingFreeSlots;

    D3D12_CPU_DESCRIPTOR_HANDLE rtvExtraCpuAt(UINT idx) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h = rtvExtraHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += idx * rtvExtraDescSize;
        return h;
    }

    // Thread-safe — mirrors allocSrvIndex().
    UINT allocRtvExtraIndex() {
        std::lock_guard<std::mutex> lock(rtvExtraMutex);
        if (!rtvExtraFreeSlots.empty()) {
            UINT idx = rtvExtraFreeSlots.back();
            rtvExtraFreeSlots.pop_back();
            return idx;
        }
        return rtvExtraOffset++;
    }

    // Thread-safe — mirrors freeSrvSlots(). Texture::~Texture() can run on
    // an ImageLoader worker thread (see srvMutex's doc comment above for
    // why), so this needs the same protection even though today's offscreen
    // render targets are only ever created/destroyed on the main thread —
    // future-proofing against that changing is cheap and matches the
    // existing SRV pattern exactly.
    void freeRtvExtraSlots(const std::vector<UINT>& indices) {
        if (indices.empty()) return;
        std::lock_guard<std::mutex> lock(rtvExtraMutex);
        rtvExtraPendingFreeSlots.insert(rtvExtraPendingFreeSlots.end(), indices.begin(), indices.end());
    }

    // Thread-safe — mirrors recycleSrvSlots(). Called once per
    // Device::submit() after waitForGpu().
    void recycleRtvExtraSlots() {
        std::lock_guard<std::mutex> lock(rtvExtraMutex);
        if (rtvExtraPendingFreeSlots.empty()) return;
        rtvExtraFreeSlots.insert(rtvExtraFreeSlots.end(), rtvExtraPendingFreeSlots.begin(), rtvExtraPendingFreeSlots.end());
        rtvExtraPendingFreeSlots.clear();
    }

    // Convenience wrapper for transient, single-command-list-recording RTV
    // use (e.g. CommandEncoder::generateMipmaps()'s per-mip-level RTV) where
    // the caller doesn't wrap the allocation in a TextureHandle to free it
    // via freeRtvExtraSlots() later. Callers that DO need to free their
    // allocation (Device::createTexture(), which stores the index in
    // TextureHandle::rtvExtraIndex) should call allocRtvExtraIndex() +
    // rtvExtraCpuAt() directly instead, as createTexture() does.
    D3D12_CPU_DESCRIPTOR_HANDLE allocRtvExtra() {
        return rtvExtraCpuAt(allocRtvExtraIndex());
    }

    D3D12_CPU_DESCRIPTOR_HANDLE allocDsv() {
        D3D12_CPU_DESCRIPTOR_HANDLE h = dsvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += dsvOffset * dsvDescSize;
        ++dsvOffset;
        return h;
    }

    // Cached command signatures for indirect draw/dispatch (created on first use).
    ID3D12CommandSignature* drawCmdSig        = nullptr;
    ID3D12CommandSignature* drawIndexedCmdSig = nullptr;
    ID3D12CommandSignature* dispatchCmdSig    = nullptr;

    // Cached mipmap generation resources (created on first use).
    ID3D12RootSignature*    mipmapRootSig     = nullptr;
    ID3DBlob*               mipmapVS          = nullptr;
    ID3DBlob*               mipmapPS          = nullptr;
    std::unordered_map<DXGI_FORMAT, ID3D12PipelineState*> mipmapPSOs;
    
    // Resource counters for metrics
    std::atomic<uint32_t> bufferCount{0};
    std::atomic<uint32_t> textureCount{0};
    std::atomic<uint32_t> renderPipelineCount{0};
    std::atomic<uint32_t> computePipelineCount{0};
    std::atomic<uint32_t> rayTracingPipelineCount{0};
    std::atomic<uint32_t> accelerationStructureCount{0};
    std::atomic<uint32_t> shaderModuleCount{0};
    std::atomic<uint32_t> samplerCount{0};
    std::atomic<uint32_t> bindGroupCount{0};
    std::atomic<uint32_t> bindGroupLayoutCount{0};
    std::atomic<uint32_t> pipelineLayoutCount{0};
    std::atomic<uint32_t> querySetCount{0};
    
    // Command stats
    std::atomic<uint64_t> commandsSubmitted{0};
    std::atomic<uint64_t> renderPasses{0};
    std::atomic<uint64_t> computePasses{0};
    std::atomic<uint64_t> rayTracingPasses{0};
    std::atomic<uint64_t> drawCalls{0};
    std::atomic<uint64_t> dispatchCalls{0};
    std::atomic<uint64_t> traceRaysCalls{0};
    std::atomic<uint64_t> copies{0};
    
    // Phase 2: Resource memory tracking (bytes)
    std::atomic<uint64_t> bufferBytes{0};
    std::atomic<uint64_t> textureBytes{0};
    std::atomic<uint64_t> accelerationStructureBytes{0};
    std::atomic<uint64_t> shaderModuleBytes{0};
    std::atomic<uint64_t> querySetBytes{0};
    
    // Phase 2: Peak memory tracking
    std::atomic<uint64_t> peakBufferBytes{0};
    std::atomic<uint64_t> peakTextureBytes{0};
    std::atomic<uint64_t> peakAccelerationStructureBytes{0};
    std::atomic<uint64_t> peakTotalBytes{0};
    
    // Phase 3: GPU pass timing (nanoseconds)
    std::atomic<uint64_t> renderPassTimeNs{0};
    std::atomic<uint64_t> computePassTimeNs{0};
    std::atomic<uint64_t> rayTracingPassTimeNs{0};
    std::atomic<uint32_t> renderPassSampleCount{0};
    std::atomic<uint32_t> computePassSampleCount{0};
    std::atomic<uint32_t> rayTracingPassSampleCount{0};
    
    // Phase 3: Memory budget and pressure management
    MemoryBudget memoryBudget;
    MemoryPressureCallback memoryPressureCallback;
    std::atomic<MemoryPressureLevel> lastPressureLevel{MemoryPressureLevel::Normal};

    void ensureDrawCommandSignature() {
        if (drawCmdSig) return;
        D3D12_INDIRECT_ARGUMENT_DESC arg = {};
        arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        D3D12_COMMAND_SIGNATURE_DESC desc = {};
        desc.ByteStride       = sizeof(D3D12_DRAW_ARGUMENTS);
        desc.NumArgumentDescs = 1;
        desc.pArgumentDescs   = &arg;
        device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&drawCmdSig));
    }

    void ensureDrawIndexedCommandSignature() {
        if (drawIndexedCmdSig) return;
        D3D12_INDIRECT_ARGUMENT_DESC arg = {};
        arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        D3D12_COMMAND_SIGNATURE_DESC desc = {};
        desc.ByteStride       = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        desc.NumArgumentDescs = 1;
        desc.pArgumentDescs   = &arg;
        device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&drawIndexedCmdSig));
    }

    void ensureDispatchCommandSignature() {
        if (dispatchCmdSig) return;
        D3D12_INDIRECT_ARGUMENT_DESC arg = {};
        arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        D3D12_COMMAND_SIGNATURE_DESC desc = {};
        desc.ByteStride       = sizeof(D3D12_DISPATCH_ARGUMENTS);
        desc.NumArgumentDescs = 1;
        desc.pArgumentDescs   = &arg;
        device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&dispatchCmdSig));
    }

    // Block the CPU until the GPU has finished all submitted work.
    void waitForGpu() {
        if (!fence || !fenceEvent) return;
        ++fenceValue;
        queue->Signal(fence, fenceValue);
        if (fence->GetCompletedValue() < fenceValue) {
            fence->SetEventOnCompletion(fenceValue, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }
};

// -----------------------------------------------------------------------
// Resource handle structs  (stored as void* in public API objects)
// -----------------------------------------------------------------------

struct BufferHandle {
    ID3D12Resource* resource  = nullptr;
    ID3D12Device*   device    = nullptr;
    ID3D12CommandQueue* queue = nullptr;  // for readback synchronization
    uint64_t        size      = 0;
    void*           mapped    = nullptr;  // persistently mapped upload-heap or readback-heap pointer
    bool            isReadback = false;   // true if created as READBACK heap
    uint64_t        allocatedSize = 0;    // Actual GPU memory allocated (includes alignment)
    DeviceData*     deviceData = nullptr; // For metrics tracking on destruction
};

struct TextureHandle {
    ID3D12Resource*     resource      = nullptr;
    ID3D12Device*       device        = nullptr;
    ID3D12CommandQueue* queue         = nullptr;
    DeviceData*         deviceData    = nullptr;
    PixelFormat     format        = PixelFormat::invalid;
    TextureType     dimension     = TextureType::tt2d;
    uint32_t        width         = 0;
    uint32_t        height        = 0;
    uint32_t        depthOrLayers = 1;
    uint32_t        mipLevels     = 1;
    uint32_t        sampleCount   = 1;
    TextureUsage    usage         = static_cast<TextureUsage>(0);
    uint64_t        allocatedSize = 0;  // Actual GPU memory allocated
    // Descriptor handles (filled by createView or createTexture for render targets)
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle    = {};
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle    = {};
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle = {};  // pre-baked SRV for shader binding
    // rtvExtraHeap slot index backing rtvHandle above, so Texture::~Texture()
    // can free it via DeviceData::freeRtvExtraSlots() — sentinel (-1) means
    // this texture was never allocated a rtvExtraHeap slot (not a render
    // target, or a depth format using dsvHandle/allocDsv() instead).
    UINT rtvExtraIndex = static_cast<UINT>(-1);

    // Current resource state (initialized in Device::createTexture() to
    // whatever InitialResourceState was passed to CreateCommittedResource,
    // then updated as CommandEncoder::beginRenderPass()/RenderPassEncoder::
    // end() transition it between RENDER_TARGET (while written as a color
    // attachment) and PIXEL_SHADER_RESOURCE|NON_PIXEL_SHADER_RESOURCE (while
    // read as a shader resource) — a texture used as an offscreen render
    // target and then sampled (BackdropFilter blur, ClipRRect/ShaderMask
    // composites) needs an explicit barrier between those two uses; D3D12
    // has no implicit tracking the way Metal/Vulkan's runtimes do. Confirmed
    // via the D3D12 debug layer: "SetGraphicsRootDescriptorTable: Resource
    // state (RENDER_TARGET) ... is invalid for use as a
    // NON_PIXEL_SHADER_RESOURCE|PIXEL_SHADER_RESOURCE" without this.
    D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
};

enum class ViewType { SRV, UAV, RTV, DSV };

struct TextureViewHandle {
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = {};
    DXGI_FORMAT                 format    = DXGI_FORMAT_UNKNOWN;
    ViewType                    viewType  = ViewType::SRV;

    // Back-reference to the owning TextureHandle, used by beginRenderPass()/
    // RenderPassEncoder::end() to read/update TextureHandle::currentState
    // and insert resource-state barriers. Left null for views that don't
    // wrap a Device::createTexture()-created texture — e.g. the swapchain's
    // own backbuffer view from getSwapchainTextureView(), which manages its
    // PRESENT<->RENDER_TARGET transition separately and must NOT be
    // transitioned to a shader-resource state.
    TextureHandle* sourceHandle = nullptr;
};

struct ShaderModuleHandle {
    std::vector<uint8_t> bytecode;
};

struct RenderPipelineHandle {
    ID3D12PipelineState*     pso           = nullptr;
    ID3D12RootSignature*     rootSignature = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY topology      = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    std::vector<UINT>        vertexStrides; // arrayStride per buffer slot
};

struct ComputePipelineHandle {
    ID3D12PipelineState*  pso           = nullptr;
    ID3D12RootSignature*  rootSignature = nullptr;
};

struct BindGroupLayoutHandle {
    std::vector<D3D12_DESCRIPTOR_RANGE1> ranges;
    uint32_t                             numDescriptors = 0;
};

struct BindGroupHandle {
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle     = {};
    D3D12_GPU_DESCRIPTOR_HANDLE samplerHandle = {};

    // srvHeap slot indices this bind group's non-sampler entries consumed
    // (CBV/SRV/AS — samplers reuse the Sampler's own permanent slot, never
    // allocate one here) — freed (deferred, via srvPendingFreeSlots) in
    // BindGroup::~BindGroup(). See DeviceData::srvPendingFreeSlots' doc
    // comment in common.hpp for why this can't be immediate.
    std::vector<UINT> srvSlotIndices;
    DeviceData*        deviceData = nullptr;
};

struct PipelineLayoutHandle {
    ID3D12RootSignature* rootSignature = nullptr;
};

struct SamplerHandle {
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = {};
    UINT                        heapIndex = 0;
};

struct QuerySetHandle {
    ID3D12QueryHeap* queryHeap    = nullptr;
    ID3D12Resource*  resultBuffer = nullptr;
    ID3D12Device*    device       = nullptr;
    uint32_t         count        = 0;
    D3D12_QUERY_TYPE queryType    = D3D12_QUERY_TYPE_TIMESTAMP;
};

struct AccelerationStructureHandle {
    ID3D12Resource* resource          = nullptr;
    ID3D12Device*   device            = nullptr;
    UINT64          gpuVA             = 0;
    UINT64          buildScratchSize  = 0;
    UINT64          updateScratchSize = 0;
};

struct RayTracingPipelineHandle {
    ID3D12StateObject*           stateObject   = nullptr;
    ID3D12StateObjectProperties* soProps       = nullptr;
    ID3D12Device*                device        = nullptr;
    ID3D12RootSignature*         rootSignature = nullptr;
    ID3D12Resource*              rayGenTable   = nullptr;
    ID3D12Resource*              missTable     = nullptr;
    ID3D12Resource*              hitGroupTable = nullptr;
    D3D12_DISPATCH_RAYS_DESC     dispatchDesc  = {};
};

struct RayTracingPassEncoderHandle {
    ID3D12GraphicsCommandList4* cmdList4      = nullptr;
    DeviceData*                 deviceData    = nullptr;
    D3D12_DISPATCH_RAYS_DESC    dispatchDesc  = {};
};

struct CommandEncoderHandle {
    ID3D12CommandAllocator*    allocator         = nullptr;
    ID3D12GraphicsCommandList* cmdList           = nullptr;
    DeviceData*                deviceData        = nullptr;
    std::vector<ID3D12Resource*> stagingResources; // freed after GPU completes (via CommandBuffer)
    
    // GPU timestamp query resources
    ID3D12QueryHeap*           timestampQueryHeap = nullptr;
    ID3D12Resource*            timestampReadbackBuffer = nullptr;
    double                     timestampFrequency = 0.0;
};

struct RenderPassEncoderHandle {
    ID3D12GraphicsCommandList* cmdList    = nullptr;
    DeviceData*                deviceData = nullptr;
    // Index buffer state for drawIndexed
    D3D12_INDEX_BUFFER_VIEW    ibv        = {};
    bool                       hasIBV     = false;
    D3D12_PRIMITIVE_TOPOLOGY   topology   = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    // Vertex strides per slot — copied from the pipeline on setPipeline()
    std::vector<UINT>          vertexStrides;
    // Whether setPipeline() has run SetGraphicsRootSignature() on cmdList yet.
    // SetGraphicsRootDescriptorTable() is undefined behavior (observed as an
    // access violation, not a clean validation error) if no root signature has
    // ever been set on the command list -- callers that call setBindGroup()
    // before setPipeline() (or a pass with no pipeline at all) must no-op
    // instead of issuing the call.
    bool                       hasRootSignature = false;
    // Occlusion query state (populated from BeginRenderPassDescriptor::occlusionQuerySet)
    ID3D12QueryHeap*           queryHeap        = nullptr;
    D3D12_QUERY_TYPE           queryType        = D3D12_QUERY_TYPE_OCCLUSION;
    uint32_t                   activeQueryIndex = 0;
    // Color-attachment textures transitioned to RENDER_TARGET by
    // beginRenderPass() (only those with a non-null TextureViewHandle::
    // sourceHandle — i.e. not the swapchain backbuffer) — transitioned to a
    // shader-readable state in end(). See TextureHandle::currentState's doc
    // comment.
    std::vector<TextureHandle*> colorAttachmentTextures;
};

struct ComputePassEncoderHandle {
    ID3D12GraphicsCommandList* cmdList     = nullptr;
    DeviceData*                deviceData  = nullptr;
};

struct CommandBufferHandle {
    ID3D12GraphicsCommandList* cmdList           = nullptr;
    ID3D12CommandAllocator*    allocator         = nullptr;
    DeviceData*                deviceData        = nullptr;
    std::vector<ID3D12Resource*> stagingResources; // upload-heap buffers kept alive until GPU completes
    
    // GPU timing data
    ID3D12QueryHeap*           timestampQueryHeap = nullptr;
    ID3D12Resource*            timestampReadbackBuffer = nullptr;
    uint64_t                   gpuStartTimestamp = 0;
    uint64_t                   gpuEndTimestamp = 0;
    bool                       hasTimingData = false;
    double                     timestampFrequency = 0.0;  // Ticks per second
};

/**
 * @brief Per-Fence DirectX 12 state.
 *
 * Each fence owns its own ID3D12Fence + event handle so that multiple fences
 * can be waited on independently (required for frame-in-flight rings).
 */
struct DirectXFenceData {
    ID3D12Fence* fence      = nullptr;
    HANDLE       fenceEvent = nullptr;
    UINT64       value      = 0;
    // Non-owning; the device outlives every fence created from it. Needed by
    // didFail()/failureReason() to call ID3D12Device::GetDeviceRemovedReason().
    ID3D12Device* device    = nullptr;
};

} // namespace systems::leal::campello_gpu

// -----------------------------------------------------------------------
// Format helpers (shared across all directx translation units)
// -----------------------------------------------------------------------

inline DXGI_FORMAT toDXGIFormat(systems::leal::campello_gpu::PixelFormat f) {
    using namespace systems::leal::campello_gpu;
    switch (f) {
        case PixelFormat::r8unorm:               return DXGI_FORMAT_R8_UNORM;
        case PixelFormat::r8snorm:               return DXGI_FORMAT_R8_SNORM;
        case PixelFormat::r8uint:                return DXGI_FORMAT_R8_UINT;
        case PixelFormat::r8sint:                return DXGI_FORMAT_R8_SINT;
        case PixelFormat::r16unorm:              return DXGI_FORMAT_R16_UNORM;
        case PixelFormat::r16snorm:              return DXGI_FORMAT_R16_SNORM;
        case PixelFormat::r16uint:               return DXGI_FORMAT_R16_UINT;
        case PixelFormat::r16sint:               return DXGI_FORMAT_R16_SINT;
        case PixelFormat::r16float:              return DXGI_FORMAT_R16_FLOAT;
        case PixelFormat::rg8unorm:              return DXGI_FORMAT_R8G8_UNORM;
        case PixelFormat::rg8snorm:              return DXGI_FORMAT_R8G8_SNORM;
        case PixelFormat::rg8uint:               return DXGI_FORMAT_R8G8_UINT;
        case PixelFormat::rg8sint:               return DXGI_FORMAT_R8G8_SINT;
        case PixelFormat::r32uint:               return DXGI_FORMAT_R32_UINT;
        case PixelFormat::r32sint:               return DXGI_FORMAT_R32_SINT;
        case PixelFormat::r32float:              return DXGI_FORMAT_R32_FLOAT;
        case PixelFormat::rg16unorm:             return DXGI_FORMAT_R16G16_UNORM;
        case PixelFormat::rg16snorm:             return DXGI_FORMAT_R16G16_SNORM;
        case PixelFormat::rg16uint:              return DXGI_FORMAT_R16G16_UINT;
        case PixelFormat::rg16sint:              return DXGI_FORMAT_R16G16_SINT;
        case PixelFormat::rg16float:             return DXGI_FORMAT_R16G16_FLOAT;
        case PixelFormat::rgba8unorm:            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::rgba8unorm_srgb:       return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case PixelFormat::rgba8snorm:            return DXGI_FORMAT_R8G8B8A8_SNORM;
        case PixelFormat::rgba8uint:             return DXGI_FORMAT_R8G8B8A8_UINT;
        case PixelFormat::rgba8sint:             return DXGI_FORMAT_R8G8B8A8_SINT;
        case PixelFormat::bgra8unorm:            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case PixelFormat::bgra8unorm_srgb:       return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case PixelFormat::rgb10a2uint:           return DXGI_FORMAT_R10G10B10A2_UINT;
        case PixelFormat::rgb10a2unorm:          return DXGI_FORMAT_R10G10B10A2_UNORM;
        case PixelFormat::rg11b10ufloat:         return DXGI_FORMAT_R11G11B10_FLOAT;
        case PixelFormat::rgb9e5ufloat:          return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
        case PixelFormat::rg32uint:              return DXGI_FORMAT_R32G32_UINT;
        case PixelFormat::rg32sint:              return DXGI_FORMAT_R32G32_SINT;
        case PixelFormat::rg32float:             return DXGI_FORMAT_R32G32_FLOAT;
        case PixelFormat::rgba16unorm:           return DXGI_FORMAT_R16G16B16A16_UNORM;
        case PixelFormat::rgba16snorm:           return DXGI_FORMAT_R16G16B16A16_SNORM;
        case PixelFormat::rgba16uint:            return DXGI_FORMAT_R16G16B16A16_UINT;
        case PixelFormat::rgba16sint:            return DXGI_FORMAT_R16G16B16A16_SINT;
        case PixelFormat::rgba16float:           return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case PixelFormat::rgba32uint:            return DXGI_FORMAT_R32G32B32A32_UINT;
        case PixelFormat::rgba32sint:            return DXGI_FORMAT_R32G32B32A32_SINT;
        case PixelFormat::rgba32float:           return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case PixelFormat::stencil8:              return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case PixelFormat::depth16unorm:          return DXGI_FORMAT_D16_UNORM;
        case PixelFormat::depth24plus_stencil8:  return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case PixelFormat::depth32float:          return DXGI_FORMAT_D32_FLOAT;
        case PixelFormat::depth32float_stencil8: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case PixelFormat::bc1_rgba_unorm:        return DXGI_FORMAT_BC1_UNORM;
        case PixelFormat::bc1_rgba_unorm_srgb:   return DXGI_FORMAT_BC1_UNORM_SRGB;
        case PixelFormat::bc2_rgba_unorm:        return DXGI_FORMAT_BC2_UNORM;
        case PixelFormat::bc2_rgba_unorm_srgb:   return DXGI_FORMAT_BC2_UNORM_SRGB;
        case PixelFormat::bc3_rgba_unorm:        return DXGI_FORMAT_BC3_UNORM;
        case PixelFormat::bc3_rgba_unorm_srgb:   return DXGI_FORMAT_BC3_UNORM_SRGB;
        case PixelFormat::bc4_r_unorm:           return DXGI_FORMAT_BC4_UNORM;
        case PixelFormat::bc4_r_snorm:           return DXGI_FORMAT_BC4_SNORM;
        case PixelFormat::bc5_rg_unorm:          return DXGI_FORMAT_BC5_UNORM;
        case PixelFormat::bc5_rg_snorm:          return DXGI_FORMAT_BC5_SNORM;
        case PixelFormat::bc6h_rgb_ufloat:       return DXGI_FORMAT_BC6H_UF16;
        case PixelFormat::bc6h_rgb_float:        return DXGI_FORMAT_BC6H_SF16;
        case PixelFormat::bc7_rgba_unorm:        return DXGI_FORMAT_BC7_UNORM;
        case PixelFormat::bc7_rgba_unorm_srgb:   return DXGI_FORMAT_BC7_UNORM_SRGB;
        default:                                 return DXGI_FORMAT_UNKNOWN;
    }
}

inline bool isDepthFormat(systems::leal::campello_gpu::PixelFormat f) {
    using namespace systems::leal::campello_gpu;
    switch (f) {
        case PixelFormat::depth16unorm:
        case PixelFormat::depth32float:
        case PixelFormat::depth24plus_stencil8:
        case PixelFormat::depth32float_stencil8:
        case PixelFormat::stencil8:
            return true;
        default:
            return false;
    }
}
