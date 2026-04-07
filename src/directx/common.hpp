#pragma once

#define NOMINMAX
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <cstdint>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/texture_usage.hpp>

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

    // Allocate one SRV slot; returns the CPU and GPU handles for it.
    D3D12_CPU_DESCRIPTOR_HANDLE allocSrvCpu() {
        D3D12_CPU_DESCRIPTOR_HANDLE h = srvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += srvOffset * srvDescSize;
        ++srvOffset;
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

    D3D12_CPU_DESCRIPTOR_HANDLE allocRtvExtra() {
        D3D12_CPU_DESCRIPTOR_HANDLE h = rtvExtraHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += rtvExtraOffset * rtvExtraDescSize;
        ++rtvExtraOffset;
        return h;
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
    // Descriptor handles (filled by createView or createTexture for render targets)
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle    = {};
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle    = {};
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle = {};  // pre-baked SRV for shader binding
};

enum class ViewType { SRV, UAV, RTV, DSV };

struct TextureViewHandle {
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = {};
    DXGI_FORMAT                 format    = DXGI_FORMAT_UNKNOWN;
    ViewType                    viewType  = ViewType::SRV;
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
    // Occlusion query state (populated from BeginRenderPassDescriptor::occlusionQuerySet)
    ID3D12QueryHeap*           queryHeap        = nullptr;
    D3D12_QUERY_TYPE           queryType        = D3D12_QUERY_TYPE_OCCLUSION;
    uint32_t                   activeQueryIndex = 0;
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
