#define NOMINMAX
#include <d3d12.h>
#include <dxgi1_6.h>
#include "campello_gpu_config.h"
#include "common.hpp"
#include <campello_gpu/device.hpp>
#include <campello_gpu/adapter.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/shader_module.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/pipeline_layout.hpp>
#include <campello_gpu/sampler.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/descriptors/render_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/compute_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/descriptors/sampler_descriptor.hpp>
#include <campello_gpu/descriptors/query_set_descriptor.hpp>
#include <campello_gpu/constants/query_set_type.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/feature.hpp>
#include <iostream>
#include <algorithm>

using namespace systems::leal::campello_gpu;

// -----------------------------------------------------------------------
// Format conversion helpers
// -----------------------------------------------------------------------

// toDXGIFormat and isDepthFormat are defined as inline helpers in common.hpp

static D3D12_BLEND toD3D12Blend(BlendFactor f) {
    switch (f) {
        case BlendFactor::zero:             return D3D12_BLEND_ZERO;
        case BlendFactor::one:              return D3D12_BLEND_ONE;
        case BlendFactor::srcColor:         return D3D12_BLEND_SRC_COLOR;
        case BlendFactor::oneMinusSrcColor: return D3D12_BLEND_INV_SRC_COLOR;
        case BlendFactor::srcAlpha:         return D3D12_BLEND_SRC_ALPHA;
        case BlendFactor::oneMinusSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
        case BlendFactor::dstColor:         return D3D12_BLEND_DEST_COLOR;
        case BlendFactor::oneMinusDstColor: return D3D12_BLEND_INV_DEST_COLOR;
        case BlendFactor::dstAlpha:         return D3D12_BLEND_DEST_ALPHA;
        case BlendFactor::oneMinusDstAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
        default:                            return D3D12_BLEND_ONE;
    }
}

static D3D12_BLEND_OP toD3D12BlendOp(BlendOperation op) {
    switch (op) {
        case BlendOperation::add:             return D3D12_BLEND_OP_ADD;
        case BlendOperation::subtract:        return D3D12_BLEND_OP_SUBTRACT;
        case BlendOperation::reverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
        case BlendOperation::min:             return D3D12_BLEND_OP_MIN;
        case BlendOperation::max:             return D3D12_BLEND_OP_MAX;
        default:                              return D3D12_BLEND_OP_ADD;
    }
}

static D3D12_COMPARISON_FUNC toD3D12Compare(CompareOp op) {
    switch (op) {
        case CompareOp::never:        return D3D12_COMPARISON_FUNC_NEVER;
        case CompareOp::less:         return D3D12_COMPARISON_FUNC_LESS;
        case CompareOp::equal:        return D3D12_COMPARISON_FUNC_EQUAL;
        case CompareOp::lessEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case CompareOp::greater:      return D3D12_COMPARISON_FUNC_GREATER;
        case CompareOp::notEqual:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case CompareOp::greaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        default:                      return D3D12_COMPARISON_FUNC_ALWAYS;
    }
}

static D3D12_STENCIL_OP toD3D12StencilOp(StencilOp op) {
    switch (op) {
        case StencilOp::zero:           return D3D12_STENCIL_OP_ZERO;
        case StencilOp::replace:        return D3D12_STENCIL_OP_REPLACE;
        case StencilOp::invert:         return D3D12_STENCIL_OP_INVERT;
        case StencilOp::incrementClamp: return D3D12_STENCIL_OP_INCR_SAT;
        case StencilOp::decrementClamp: return D3D12_STENCIL_OP_DECR_SAT;
        case StencilOp::incrementWrap:  return D3D12_STENCIL_OP_INCR;
        case StencilOp::decrementWrap:  return D3D12_STENCIL_OP_DECR;
        default:                        return D3D12_STENCIL_OP_KEEP;
    }
}

static DXGI_FORMAT toDXGIVertexFormat(ComponentType comp, AccessorType acc) {
    switch (acc) {
        case AccessorType::acScalar:
            switch (comp) {
                case ComponentType::ctFloat:         return DXGI_FORMAT_R32_FLOAT;
                case ComponentType::ctUnsignedInt:   return DXGI_FORMAT_R32_UINT;
                case ComponentType::ctShort:         return DXGI_FORMAT_R16_SINT;
                case ComponentType::ctUnsignedShort: return DXGI_FORMAT_R16_UINT;
                case ComponentType::ctByte:          return DXGI_FORMAT_R8_SINT;
                case ComponentType::ctUnsignedByte:  return DXGI_FORMAT_R8_UINT;
                default:                             return DXGI_FORMAT_R32_FLOAT;
            }
        case AccessorType::acVec2:
            switch (comp) {
                case ComponentType::ctFloat:         return DXGI_FORMAT_R32G32_FLOAT;
                case ComponentType::ctUnsignedInt:   return DXGI_FORMAT_R32G32_UINT;
                case ComponentType::ctShort:         return DXGI_FORMAT_R16G16_SINT;
                case ComponentType::ctUnsignedShort: return DXGI_FORMAT_R16G16_UINT;
                case ComponentType::ctByte:          return DXGI_FORMAT_R8G8_SINT;
                case ComponentType::ctUnsignedByte:  return DXGI_FORMAT_R8G8_UINT;
                default:                             return DXGI_FORMAT_R32G32_FLOAT;
            }
        case AccessorType::acVec3:
            switch (comp) {
                case ComponentType::ctFloat:         return DXGI_FORMAT_R32G32B32_FLOAT;
                case ComponentType::ctUnsignedInt:   return DXGI_FORMAT_R32G32B32_UINT;
                default:                             return DXGI_FORMAT_R32G32B32_FLOAT;
            }
        case AccessorType::acVec4:
            switch (comp) {
                case ComponentType::ctFloat:         return DXGI_FORMAT_R32G32B32A32_FLOAT;
                case ComponentType::ctUnsignedInt:   return DXGI_FORMAT_R32G32B32A32_UINT;
                case ComponentType::ctShort:         return DXGI_FORMAT_R16G16B16A16_SINT;
                case ComponentType::ctUnsignedShort: return DXGI_FORMAT_R16G16B16A16_UINT;
                case ComponentType::ctByte:          return DXGI_FORMAT_R8G8B8A8_SINT;
                case ComponentType::ctUnsignedByte:  return DXGI_FORMAT_R8G8B8A8_UINT;
                default:                             return DXGI_FORMAT_R32G32B32A32_FLOAT;
            }
        default:
            return DXGI_FORMAT_R32_FLOAT;
    }
}

static D3D12_PRIMITIVE_TOPOLOGY toPrimitiveTopology(PrimitiveTopology t) {
    switch (t) {
        case PrimitiveTopology::pointList:     return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case PrimitiveTopology::lineList:      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case PrimitiveTopology::lineStrip:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case PrimitiveTopology::triangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        default:                               return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

// -----------------------------------------------------------------------
// DeviceData creation helper
// -----------------------------------------------------------------------

static DeviceData* createDeviceData(IDXGIAdapter1* adapter, void* pd) {
    auto* data = new DeviceData();
    data->adapter = adapter;

    if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0,
                                  IID_PPV_ARGS(&data->device)))) {
        delete data;
        return nullptr;
    }
    auto* dev = data->device;

    // Command queue
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&data->queue)))) {
        dev->Release(); delete data; return nullptr;
    }

    // CBV/SRV/UAV heap — 1024 shader-visible descriptors
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.NumDescriptors = 1024;
        hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&data->srvHeap));
        data->srvDescSize = dev->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // Sampler heap — 128 shader-visible
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.NumDescriptors = 128;
        hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&data->samplerHeap));
        data->samplerDescSize = dev->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }

    // RTV heap for user render targets (CPU-only, 64 slots)
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.NumDescriptors = 64;
        hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&data->rtvExtraHeap));
        data->rtvExtraDescSize = dev->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // DSV heap (CPU-only, 32 slots)
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.NumDescriptors = 32;
        hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&data->dsvHeap));
        data->dsvDescSize = dev->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }

    // Fence + event
    dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&data->fence));
    data->fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // Swapchain (optional — only when a window handle is provided)
    if (pd != nullptr) {
        HWND hwnd = static_cast<HWND>(pd);
        data->hwnd = hwnd;
        RECT rect = {};
        GetClientRect(hwnd, &rect);
        UINT w = std::max<UINT>(rect.right  - rect.left, 1u);
        UINT h = std::max<UINT>(rect.bottom - rect.top,  1u);

        IDXGIFactory6* factory = nullptr;
        CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

        DXGI_SWAP_CHAIN_DESC1 scd = {};
        scd.BufferCount = DeviceData::kFrameCount;
        scd.Width       = w;
        scd.Height      = h;
        scd.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scd.SampleDesc.Count = 1;

        IDXGISwapChain1* sc1 = nullptr;
        factory->CreateSwapChainForHwnd(data->queue, hwnd, &scd,
                                        nullptr, nullptr, &sc1);
        if (sc1) {
            sc1->QueryInterface(IID_PPV_ARGS(&data->swapChain));
            sc1->Release();
        }
        factory->Release();

        if (data->swapChain) {
            // RTV heap for swapchain back buffers
            D3D12_DESCRIPTOR_HEAP_DESC rd = {};
            rd.NumDescriptors = DeviceData::kFrameCount;
            rd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            dev->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&data->rtvHeap));
            data->rtvDescSize = dev->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvH =
                data->rtvHeap->GetCPUDescriptorHandleForHeapStart();
            for (UINT i = 0; i < DeviceData::kFrameCount; ++i) {
                data->swapChain->GetBuffer(i, IID_PPV_ARGS(&data->renderTargets[i]));
                dev->CreateRenderTargetView(data->renderTargets[i], nullptr, rtvH);
                rtvH.ptr += data->rtvDescSize;
            }
            data->frameIndex = data->swapChain->GetCurrentBackBufferIndex();
        }
    }

    return data;
}

// -----------------------------------------------------------------------
// Root signature helper — creates a minimal universal root signature
// -----------------------------------------------------------------------

static ID3D12RootSignature* createUniversalRootSignature(ID3D12Device* device,
    const std::vector<BindGroupLayoutHandle*>& layouts) {

    // Build descriptor tables from each bind group layout.
    std::vector<D3D12_ROOT_PARAMETER1> params;
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE1>> allRanges;

    for (auto* layout : layouts) {
        if (!layout || layout->ranges.empty()) continue;
        allRanges.push_back(layout->ranges);
        D3D12_ROOT_PARAMETER1 p = {};
        p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p.DescriptorTable.NumDescriptorRanges =
            static_cast<UINT>(allRanges.back().size());
        p.DescriptorTable.pDescriptorRanges   = allRanges.back().data();
        p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params.push_back(p);
    }

    // If no layouts, create an empty root signature.
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.NumParameters     = static_cast<UINT>(params.size());
    desc.Desc_1_1.pParameters       = params.empty() ? nullptr : params.data();
    desc.Desc_1_1.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* blob  = nullptr;
    ID3DBlob* error = nullptr;
    if (FAILED(D3D12SerializeVersionedRootSignature(&desc, &blob, &error))) {
        if (error) error->Release();
        return nullptr;
    }

    ID3D12RootSignature* rs = nullptr;
    device->CreateRootSignature(0, blob->GetBufferPointer(),
                                blob->GetBufferSize(), IID_PPV_ARGS(&rs));
    blob->Release();
    if (error) error->Release();
    return rs;
}

// -----------------------------------------------------------------------
// Device
// -----------------------------------------------------------------------

Device::Device(void* pd) : native(pd) {
    auto* d = static_cast<DeviceData*>(pd);
    std::cout << "  - name: " << getName() << std::endl;
}

Device::~Device() {
    if (!native) return;
    auto* d = static_cast<DeviceData*>(native);
    d->waitForGpu();

    if (d->fenceEvent) CloseHandle(d->fenceEvent);
    if (d->fence)      d->fence->Release();

    for (UINT i = 0; i < DeviceData::kFrameCount; ++i)
        if (d->renderTargets[i]) d->renderTargets[i]->Release();
    if (d->rtvHeap)      d->rtvHeap->Release();
    if (d->swapChain)    d->swapChain->Release();

    if (d->drawCmdSig)        d->drawCmdSig->Release();
    if (d->drawIndexedCmdSig) d->drawIndexedCmdSig->Release();
    if (d->dispatchCmdSig)    d->dispatchCmdSig->Release();

    if (d->srvHeap)      d->srvHeap->Release();
    if (d->samplerHeap)  d->samplerHeap->Release();
    if (d->rtvExtraHeap) d->rtvExtraHeap->Release();
    if (d->dsvHeap)      d->dsvHeap->Release();

    if (d->queue)   d->queue->Release();
    if (d->device)  d->device->Release();
    if (d->adapter) d->adapter->Release();
    delete d;
}

std::shared_ptr<Device> Device::createDefaultDevice(void* pd) {
    IDXGIFactory6* factory = nullptr;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return nullptr;

    IDXGIAdapter1* adapter = nullptr;
    // Prefer the highest-performance adapter.
    if (FAILED(factory->EnumAdapterByGpuPreference(
            0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)))) {
        factory->EnumAdapters1(0, &adapter);
    }
    factory->Release();
    if (!adapter) return nullptr;

    auto* data = createDeviceData(adapter, pd);
    if (!data) { adapter->Release(); return nullptr; }
    return std::shared_ptr<Device>(new Device(data));
}

std::shared_ptr<Device> Device::createDevice(std::shared_ptr<Adapter> adapter, void* pd) {
    if (!adapter || !adapter->native) return nullptr;
    auto* dxgiAdapter = static_cast<IDXGIAdapter1*>(adapter->native);
    dxgiAdapter->AddRef();
    auto* data = createDeviceData(dxgiAdapter, pd);
    if (!data) { dxgiAdapter->Release(); return nullptr; }
    return std::shared_ptr<Device>(new Device(data));
}

std::vector<std::shared_ptr<Adapter>> Device::getAdapters() {
    std::vector<std::shared_ptr<Adapter>> result;
    IDXGIFactory6* factory = nullptr;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return result;

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0;
         factory->EnumAdapterByGpuPreference(
             i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
             IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
         ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { adapter->Release(); continue; }
        auto* a  = new Adapter();
        a->native = adapter;  // adapter's AddRef came from EnumAdapter
        result.push_back(std::shared_ptr<Adapter>(a));
        adapter = nullptr;
    }
    factory->Release();
    return result;
}

std::string Device::getName() {
    auto* d = static_cast<DeviceData*>(native);
    DXGI_ADAPTER_DESC1 desc = {};
    d->adapter->GetDesc1(&desc);
    int len = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                                  nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                        s.data(), len, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::set<Feature> Device::getFeatures() {
    auto* d = static_cast<DeviceData*>(native);
    std::set<Feature> features;

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5 = {};
    if (SUCCEEDED(d->device->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5)))) {
        if (opts5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
            features.insert(Feature::raytracing);
    }
    D3D12_FEATURE_DATA_FORMAT_SUPPORT fmt = {};
    fmt.Format = DXGI_FORMAT_BC7_UNORM;
    d->device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fmt, sizeof(fmt));
    if (fmt.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D)
        features.insert(Feature::bcTextureCompression);

    return features;
}

std::string Device::getEngineVersion() {
    const D3D_FEATURE_LEVEL levels[] = {
#ifdef D3D_FEATURE_LEVEL_12_2
        D3D_FEATURE_LEVEL_12_2,
#endif
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
    };
    for (auto level : levels) {
        if (SUCCEEDED(D3D12CreateDevice(nullptr, level,
                                        __uuidof(ID3D12Device), nullptr))) {
            switch (level) {
#ifdef D3D_FEATURE_LEVEL_12_2
                case D3D_FEATURE_LEVEL_12_2: return "Direct3D 12.2";
#endif
                case D3D_FEATURE_LEVEL_12_1: return "Direct3D 12.1";
                case D3D_FEATURE_LEVEL_12_0: return "Direct3D 12.0";
                default: break;
            }
        }
    }
    return "Direct3D 12";
}

// -----------------------------------------------------------------------
// Resource creation
// -----------------------------------------------------------------------

std::shared_ptr<Buffer> Device::createBuffer(uint64_t size, BufferUsage usage) {
    auto* d   = static_cast<DeviceData*>(native);
    auto* dev = d->device;

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = (size + 255) & ~255ULL;  // align to 256 bytes
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags            = D3D12_RESOURCE_FLAG_NONE;

    ID3D12Resource* resource = nullptr;
    if (FAILED(dev->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&resource)))) return nullptr;

    auto* handle    = new BufferHandle();
    handle->resource = resource;
    handle->device   = dev;
    handle->size     = size;

    // Persistently map the upload heap
    D3D12_RANGE readRange = { 0, 0 };
    resource->Map(0, &readRange, &handle->mapped);

    return std::shared_ptr<Buffer>(new Buffer(handle));
}

std::shared_ptr<Texture> Device::createTexture(
    TextureType type, PixelFormat pixelFormat,
    uint32_t width, uint32_t height, uint32_t depth,
    uint32_t mipLevels, uint32_t samples, TextureUsage usageMode) {

    auto* d   = static_cast<DeviceData*>(native);
    auto* dev = d->device;

    DXGI_FORMAT fmt = toDXGIFormat(pixelFormat);
    if (fmt == DXGI_FORMAT_UNKNOWN) return nullptr;

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd = {};
    rd.Width            = std::max(width, 1u);
    rd.Height           = std::max(height, 1u);
    rd.DepthOrArraySize = static_cast<UINT16>(std::max(depth, 1u));
    rd.MipLevels        = static_cast<UINT16>(std::max(mipLevels, 1u));
    rd.Format           = fmt;
    rd.SampleDesc.Count = std::max(samples, 1u);

    const int u = static_cast<int>(usageMode);
    rd.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (u & static_cast<int>(TextureUsage::renderTarget)) {
        if (isDepthFormat(pixelFormat))
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        else
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (u & static_cast<int>(TextureUsage::storageBinding))
        rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    switch (type) {
        case TextureType::tt1d:
            rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
            break;
        case TextureType::tt3d:
            rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            break;
        default:
            rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            break;
    }

    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_CLEAR_VALUE clearVal = {};
    D3D12_CLEAR_VALUE* pClear = nullptr;
    if (u & static_cast<int>(TextureUsage::renderTarget)) {
        if (isDepthFormat(pixelFormat)) {
            initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            clearVal.Format               = fmt;
            clearVal.DepthStencil.Depth   = 1.0f;
            clearVal.DepthStencil.Stencil = 0;
            pClear = &clearVal;
        } else {
            initialState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            clearVal.Format   = fmt;
            clearVal.Color[3] = 1.0f;
            pClear = &clearVal;
        }
    }

    ID3D12Resource* resource = nullptr;
    if (FAILED(dev->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            initialState, pClear, IID_PPV_ARGS(&resource)))) return nullptr;

    auto* handle           = new TextureHandle();
    handle->resource       = resource;
    handle->device         = dev;
    handle->format         = pixelFormat;
    handle->dimension      = type;
    handle->width          = width;
    handle->height         = height;
    handle->depthOrLayers  = std::max(depth, 1u);
    handle->mipLevels      = std::max(mipLevels, 1u);
    handle->sampleCount    = std::max(samples, 1u);
    handle->usage          = usageMode;
    handle->queue          = d->queue;

    // Pre-create the RTV/DSV if this texture is a render target
    if (u & static_cast<int>(TextureUsage::renderTarget)) {
        if (isDepthFormat(pixelFormat)) {
            handle->dsvHandle = d->allocDsv();
            dev->CreateDepthStencilView(resource, nullptr, handle->dsvHandle);
        } else {
            handle->rtvHandle = d->allocRtvExtra();
            dev->CreateRenderTargetView(resource, nullptr, handle->rtvHandle);
        }
    }

    return std::shared_ptr<Texture>(new Texture(handle));
}

std::shared_ptr<ShaderModule> Device::createShaderModule(
    const uint8_t* buffer, uint64_t size) {
    auto* h = new ShaderModuleHandle();
    h->bytecode.assign(buffer, buffer + size);
    return std::shared_ptr<ShaderModule>(new ShaderModule(h));
}

std::shared_ptr<RenderPipeline> Device::createRenderPipeline(
    const RenderPipelineDescriptor& descriptor) {

    auto* d   = static_cast<DeviceData*>(native);
    auto* dev = d->device;

    // RenderPipelineDescriptor has no layout field — use an empty root signature.
    ID3D12RootSignature* rootSig = createUniversalRootSignature(dev, {});
    if (!rootSig) return nullptr;

    // Input layout from vertex descriptor
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
    for (size_t bi = 0; bi < descriptor.vertex.buffers.size(); ++bi) {
        const auto& layout = descriptor.vertex.buffers[bi];
        for (const auto& attr : layout.attributes) {
            D3D12_INPUT_ELEMENT_DESC e = {};
            e.SemanticName         = "TEXCOORD";
            e.SemanticIndex        = attr.shaderLocation;
            e.Format               = toDXGIVertexFormat(attr.componentType, attr.accessorType);
            e.InputSlot            = static_cast<UINT>(bi);
            e.AlignedByteOffset    = static_cast<UINT>(attr.offset);
            e.InputSlotClass       = (layout.stepMode == StepMode::instance)
                ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            e.InstanceDataStepRate = (layout.stepMode == StepMode::instance) ? 1 : 0;
            inputElements.push_back(e);
        }
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psd = {};
    psd.pRootSignature = rootSig;

    // Vertex shader
    if (descriptor.vertex.module) {
        auto* sm = static_cast<ShaderModuleHandle*>(descriptor.vertex.module->native);
        psd.VS = { sm->bytecode.data(), sm->bytecode.size() };
    }

    // Pixel shader
    if (descriptor.fragment && descriptor.fragment->module) {
        auto* sm = static_cast<ShaderModuleHandle*>(descriptor.fragment->module->native);
        psd.PS = { sm->bytecode.data(), sm->bytecode.size() };
    }

    // Input layout
    psd.InputLayout = { inputElements.data(), static_cast<UINT>(inputElements.size()) };

    // Rasterizer state
    psd.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    psd.RasterizerState.FrontCounterClockwise =
        (descriptor.frontFace == FrontFace::ccw) ? TRUE : FALSE;
    switch (descriptor.cullMode) {
        case CullMode::back:  psd.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;  break;
        case CullMode::front: psd.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT; break;
        default:              psd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;  break;
    }
    psd.RasterizerState.DepthClipEnable = TRUE;
    psd.RasterizerState.DepthBias       =
        descriptor.depthStencil ? static_cast<INT>(descriptor.depthStencil->depthBias) : 0;
    psd.RasterizerState.DepthBiasClamp  =
        descriptor.depthStencil ? descriptor.depthStencil->depthBiasClamp : 0.0f;
    psd.RasterizerState.SlopeScaledDepthBias =
        descriptor.depthStencil ? descriptor.depthStencil->depthBiasSlopeScale : 0.0f;

    // Blend state — one entry per color target
    psd.BlendState.AlphaToCoverageEnable = FALSE;
    if (descriptor.fragment) {
        psd.BlendState.IndependentBlendEnable = descriptor.fragment->targets.size() > 1 ? TRUE : FALSE;
        for (size_t i = 0; i < descriptor.fragment->targets.size() && i < 8; ++i) {
            const auto& cs = descriptor.fragment->targets[i];
            auto& rt = psd.BlendState.RenderTarget[i];
            rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            if (cs.blend) {
                rt.BlendEnable    = TRUE;
                rt.SrcBlend       = toD3D12Blend(cs.blend->color.srcFactor);
                rt.DestBlend      = toD3D12Blend(cs.blend->color.dstFactor);
                rt.BlendOp        = toD3D12BlendOp(cs.blend->color.operation);
                rt.SrcBlendAlpha  = toD3D12Blend(cs.blend->alpha.srcFactor);
                rt.DestBlendAlpha = toD3D12Blend(cs.blend->alpha.dstFactor);
                rt.BlendOpAlpha   = toD3D12BlendOp(cs.blend->alpha.operation);
            } else {
                rt.BlendEnable    = FALSE;
                rt.SrcBlend       = D3D12_BLEND_ONE;
                rt.DestBlend      = D3D12_BLEND_ZERO;
                rt.BlendOp        = D3D12_BLEND_OP_ADD;
                rt.SrcBlendAlpha  = D3D12_BLEND_ONE;
                rt.DestBlendAlpha = D3D12_BLEND_ZERO;
                rt.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
            }
        }
    } else {
        psd.BlendState.IndependentBlendEnable = FALSE;
        psd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    // Depth-stencil state
    if (descriptor.depthStencil) {
        const auto& ds = *descriptor.depthStencil;
        psd.DepthStencilState.DepthEnable    = ds.depthCompare != CompareOp::always ? TRUE : TRUE;
        psd.DepthStencilState.DepthWriteMask =
            ds.depthWriteEnabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        psd.DepthStencilState.DepthFunc      = toD3D12Compare(ds.depthCompare);
        if (ds.stencilFront || ds.stencilBack) {
            psd.DepthStencilState.StencilEnable = TRUE;
            psd.DepthStencilState.StencilReadMask  = 0xFF;
            psd.DepthStencilState.StencilWriteMask = 0xFF;
            auto setFace = [](D3D12_DEPTH_STENCILOP_DESC& out, const StencilDescriptor& s) {
                out.StencilFailOp      = toD3D12StencilOp(s.failOp);
                out.StencilDepthFailOp = toD3D12StencilOp(s.depthFailOp);
                out.StencilPassOp      = toD3D12StencilOp(s.passOp);
                out.StencilFunc        = toD3D12Compare(s.compare);
            };
            if (ds.stencilFront) setFace(psd.DepthStencilState.FrontFace, *ds.stencilFront);
            if (ds.stencilBack)  setFace(psd.DepthStencilState.BackFace,  *ds.stencilBack);
        }
        psd.DSVFormat = toDXGIFormat(ds.format);
    } else {
        psd.DepthStencilState.DepthEnable   = FALSE;
        psd.DepthStencilState.StencilEnable = FALSE;
    }

    // Render target formats
    if (descriptor.fragment) {
        psd.NumRenderTargets = static_cast<UINT>(descriptor.fragment->targets.size());
        for (size_t i = 0; i < descriptor.fragment->targets.size() && i < 8; ++i)
            psd.RTVFormats[i] = toDXGIFormat(descriptor.fragment->targets[i].format);
    } else {
        psd.NumRenderTargets = 1;
        psd.RTVFormats[0]    = DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    // Topology type
    switch (descriptor.topology) {
        case PrimitiveTopology::pointList:
            psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT; break;
        case PrimitiveTopology::lineList:
        case PrimitiveTopology::lineStrip:
            psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;  break;
        default:
            psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; break;
    }

    psd.SampleMask        = UINT_MAX;
    psd.SampleDesc.Count  = 1;

    ID3D12PipelineState* pso = nullptr;
    if (FAILED(dev->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&pso)))) {
        rootSig->Release(); return nullptr;
    }

    auto* h           = new RenderPipelineHandle();
    h->pso            = pso;
    h->rootSignature  = rootSig;
    h->topology       = toPrimitiveTopology(descriptor.topology);
    h->vertexStrides.reserve(descriptor.vertex.buffers.size());
    for (const auto& layout : descriptor.vertex.buffers)
        h->vertexStrides.push_back(static_cast<UINT>(layout.arrayStride));
    return std::shared_ptr<RenderPipeline>(new RenderPipeline(h));
}

std::shared_ptr<ComputePipeline> Device::createComputePipeline(
    const ComputePipelineDescriptor& descriptor) {

    auto* d   = static_cast<DeviceData*>(native);
    auto* dev = d->device;
    if (!descriptor.compute.module) return nullptr;

    ID3D12RootSignature* rootSig = nullptr;
    if (descriptor.layout) {
        auto* plh = static_cast<PipelineLayoutHandle*>(descriptor.layout->native);
        rootSig = plh ? plh->rootSignature : nullptr;
        if (rootSig) rootSig->AddRef();
    }
    if (!rootSig) rootSig = createUniversalRootSignature(dev, {});
    if (!rootSig) return nullptr;

    auto* sm = static_cast<ShaderModuleHandle*>(descriptor.compute.module->native);
    D3D12_COMPUTE_PIPELINE_STATE_DESC psd = {};
    psd.pRootSignature = rootSig;
    psd.CS             = { sm->bytecode.data(), sm->bytecode.size() };

    ID3D12PipelineState* pso = nullptr;
    if (FAILED(dev->CreateComputePipelineState(&psd, IID_PPV_ARGS(&pso)))) {
        rootSig->Release(); return nullptr;
    }

    auto* h          = new ComputePipelineHandle();
    h->pso           = pso;
    h->rootSignature = rootSig;
    return std::shared_ptr<ComputePipeline>(new ComputePipeline(h));
}

std::shared_ptr<BindGroupLayout> Device::createBindGroupLayout(
    const BindGroupLayoutDescriptor& descriptor) {

    auto* h = new BindGroupLayoutHandle();
    for (const auto& entry : descriptor.entries) {
        D3D12_DESCRIPTOR_RANGE1 range = {};
        range.RegisterSpace             = 0;
        range.BaseShaderRegister        = entry.binding;
        range.NumDescriptors            = 1;
        range.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

        switch (entry.type) {
            case EntryObjectType::buffer:
                range.RangeType = (entry.data.buffer.type == EntryObjectBufferType::uniform)
                    ? D3D12_DESCRIPTOR_RANGE_TYPE_CBV
                    : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                break;
            case EntryObjectType::texture:
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                break;
            case EntryObjectType::sampler:
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                break;
        }
        h->ranges.push_back(range);
        ++h->numDescriptors;
    }
    return std::shared_ptr<BindGroupLayout>(new BindGroupLayout(h));
}

std::shared_ptr<BindGroup> Device::createBindGroup(
    const BindGroupDescriptor& descriptor) {
    // Bind groups in D3D12 are resolved at command recording time by
    // setting descriptor tables. Store the GPU handle for the first
    // descriptor in the SRV heap that was allocated when creating the
    // resources in this group.
    auto* h = new BindGroupHandle();
    return std::shared_ptr<BindGroup>(new BindGroup(h));
}

std::shared_ptr<PipelineLayout> Device::createPipelineLayout(
    const PipelineLayoutDescriptor& descriptor) {

    auto* d   = static_cast<DeviceData*>(native);
    auto* dev = d->device;

    // PipelineLayoutDescriptor is currently a placeholder with no bind group layouts.
    auto* rs = createUniversalRootSignature(dev, {});
    if (!rs) return nullptr;

    auto* h         = new PipelineLayoutHandle();
    h->rootSignature = rs;
    return std::shared_ptr<PipelineLayout>(new PipelineLayout(h));
}

std::shared_ptr<Sampler> Device::createSampler(const SamplerDescriptor& descriptor) {
    auto* d   = static_cast<DeviceData*>(native);
    auto* dev = d->device;

    auto toAddressMode = [](WrapMode m) -> D3D12_TEXTURE_ADDRESS_MODE {
        switch (m) {
            case WrapMode::repeat:       return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            case WrapMode::mirrorRepeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
            default:                     return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        }
    };
    auto toFilter = [](FilterMode mag, FilterMode min) -> D3D12_FILTER {
        bool magLin = (mag == FilterMode::fmLinear ||
                       mag == FilterMode::fmLinearMipmapLinear ||
                       mag == FilterMode::fmLinearMipmapNearest);
        bool minLin = (min == FilterMode::fmLinear ||
                       min == FilterMode::fmLinearMipmapLinear ||
                       min == FilterMode::fmLinearMipmapNearest);
        bool mipLin = (min == FilterMode::fmLinearMipmapLinear ||
                       min == FilterMode::fmNearestMipmapLinear);
        if (magLin && minLin && mipLin) return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        if (magLin && minLin)           return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        if (minLin)                     return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
        return D3D12_FILTER_MIN_MAG_MIP_POINT;
    };

    D3D12_SAMPLER_DESC sd = {};
    sd.Filter        = toFilter(descriptor.magFilter, descriptor.minFilter);
    sd.AddressU      = toAddressMode(descriptor.addressModeU);
    sd.AddressV      = toAddressMode(descriptor.addressModeV);
    sd.AddressW      = toAddressMode(descriptor.addressModeW);
    sd.MipLODBias    = 0.0f;
    sd.MaxAnisotropy = static_cast<UINT>(std::max(1.0, descriptor.maxAnisotropy));
    sd.ComparisonFunc = descriptor.compare
        ? toD3D12Compare(*descriptor.compare) : D3D12_COMPARISON_FUNC_NEVER;
    sd.MinLOD = static_cast<float>(descriptor.lodMinClamp);
    sd.MaxLOD = static_cast<float>(descriptor.lodMaxClamp);

    UINT idx = d->samplerOffset;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = d->allocSamplerCpu();
    dev->CreateSampler(&sd, cpu);

    auto* h      = new SamplerHandle();
    h->cpuHandle = cpu;
    h->gpuHandle = d->samplerGpuAt(idx);
    h->heapIndex = idx;
    return std::shared_ptr<Sampler>(new Sampler(h));
}

std::shared_ptr<QuerySet> Device::createQuerySet(const QuerySetDescriptor& descriptor) {
    auto* d   = static_cast<DeviceData*>(native);
    auto* dev = d->device;

    D3D12_QUERY_HEAP_DESC qhd = {};
    qhd.Count = descriptor.count;
    qhd.Type  = (descriptor.type == QuerySetType::timestamp)
                    ? D3D12_QUERY_HEAP_TYPE_TIMESTAMP
                    : D3D12_QUERY_HEAP_TYPE_OCCLUSION;

    ID3D12QueryHeap* queryHeap = nullptr;
    if (FAILED(dev->CreateQueryHeap(&qhd, IID_PPV_ARGS(&queryHeap)))) return nullptr;

    // Readback buffer for resolving query results
    D3D12_HEAP_PROPERTIES hp  = { D3D12_HEAP_TYPE_READBACK };
    D3D12_RESOURCE_DESC   rbd = {};
    rbd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rbd.Width            = descriptor.count * sizeof(uint64_t);
    rbd.Height           = 1;
    rbd.DepthOrArraySize = 1;
    rbd.MipLevels        = 1;
    rbd.Format           = DXGI_FORMAT_UNKNOWN;
    rbd.SampleDesc.Count = 1;
    rbd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* resultBuf = nullptr;
    dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rbd,
                                  D3D12_RESOURCE_STATE_COPY_DEST,
                                  nullptr, IID_PPV_ARGS(&resultBuf));

    auto* h          = new QuerySetHandle();
    h->queryHeap     = queryHeap;
    h->resultBuffer  = resultBuf;
    h->device        = dev;
    h->count         = descriptor.count;
    h->queryType     = (descriptor.type == QuerySetType::timestamp)
                           ? D3D12_QUERY_TYPE_TIMESTAMP
                           : D3D12_QUERY_TYPE_OCCLUSION;
    return std::shared_ptr<QuerySet>(new QuerySet(h));
}

std::shared_ptr<CommandEncoder> Device::createCommandEncoder() {
    auto* d   = static_cast<DeviceData*>(native);
    auto* dev = d->device;

    ID3D12CommandAllocator* alloc = nullptr;
    if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&alloc)))) return nullptr;

    ID3D12GraphicsCommandList* list = nullptr;
    if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       alloc, nullptr, IID_PPV_ARGS(&list)))) {
        alloc->Release(); return nullptr;
    }

    // Set shader-visible heaps for this command list
    ID3D12DescriptorHeap* heaps[] = { d->srvHeap, d->samplerHeap };
    list->SetDescriptorHeaps(2, heaps);

    // Transition the swapchain back buffer from PRESENT to RENDER_TARGET
    if (d->swapChain && d->renderTargets[d->frameIndex]) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = d->renderTargets[d->frameIndex];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);
    }

    auto* h          = new CommandEncoderHandle();
    h->allocator     = alloc;
    h->cmdList       = list;
    h->deviceData    = d;
    return std::shared_ptr<CommandEncoder>(new CommandEncoder(h));
}

void Device::submit(std::shared_ptr<CommandBuffer> commandBuffer) {
    auto* h  = static_cast<CommandBufferHandle*>(commandBuffer->native);
    auto* d  = h->deviceData;

    ID3D12CommandList* lists[] = { h->cmdList };
    d->queue->ExecuteCommandLists(1, lists);

    if (d->swapChain) {
        d->swapChain->Present(1, 0);
        d->frameIndex = d->swapChain->GetCurrentBackBufferIndex();
    }

    d->waitForGpu();
}


std::shared_ptr<TextureView> Device::getSwapchainTextureView() {
    auto* d = static_cast<DeviceData*>(native);
    if (!d->swapChain || !d->rtvHeap) return nullptr;

    // Lazily resize the swapchain if the window dimensions have changed.
    if (d->hwnd) {
        RECT rect = {};
        GetClientRect(d->hwnd, &rect);
        UINT w = std::max<UINT>(rect.right  - rect.left, 1u);
        UINT h = std::max<UINT>(rect.bottom - rect.top,  1u);

        DXGI_SWAP_CHAIN_DESC1 desc = {};
        d->swapChain->GetDesc1(&desc);

        if (w != desc.Width || h != desc.Height) {
            d->waitForGpu();
            for (UINT i = 0; i < DeviceData::kFrameCount; ++i) {
                if (d->renderTargets[i]) {
                    d->renderTargets[i]->Release();
                    d->renderTargets[i] = nullptr;
                }
            }
            if (SUCCEEDED(d->swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) {
                D3D12_CPU_DESCRIPTOR_HANDLE rtvH = d->rtvHeap->GetCPUDescriptorHandleForHeapStart();
                for (UINT i = 0; i < DeviceData::kFrameCount; ++i) {
                    d->swapChain->GetBuffer(i, IID_PPV_ARGS(&d->renderTargets[i]));
                    d->device->CreateRenderTargetView(d->renderTargets[i], nullptr, rtvH);
                    rtvH.ptr += d->rtvDescSize;
                }
                d->frameIndex = d->swapChain->GetCurrentBackBufferIndex();
            }
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtvH = d->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvH.ptr += d->frameIndex * d->rtvDescSize;

    auto* vh       = new TextureViewHandle();
    vh->cpuHandle  = rtvH;
    vh->format     = DXGI_FORMAT_R8G8B8A8_UNORM;
    vh->viewType   = ViewType::RTV;
    return std::shared_ptr<TextureView>(new TextureView(vh));
}

std::string systems::leal::campello_gpu::getVersion() {
    return std::to_string(campello_gpu_VERSION_MAJOR) + "." +
           std::to_string(campello_gpu_VERSION_MINOR) + "." +
           std::to_string(campello_gpu_VERSION_PATCH);
}
