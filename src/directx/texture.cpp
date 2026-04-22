#define NOMINMAX
#include "common.hpp"
#include <campello_gpu/texture.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/aspect.hpp>
#include <algorithm>

// Inline helper for calculating subresource index
inline UINT CalcSubresource(UINT mipSlice, UINT arraySlice, UINT planeSlice,
                            UINT mipLevels, UINT arraySize) {
    return mipSlice + arraySlice * mipLevels + planeSlice * mipLevels * arraySize;
}

using namespace systems::leal::campello_gpu;

Texture::Texture(void* pd) : native(pd) {}

Texture::~Texture() {
    if (!native) return;
    auto* h = static_cast<TextureHandle*>(native);
    
    // Phase 2: Update memory tracking
    if (h->deviceData) {
        h->deviceData->textureCount--;
        h->deviceData->textureBytes.fetch_sub(h->allocatedSize);
    }
    
    if (h->resource) h->resource->Release();
    delete h;
}

std::shared_ptr<TextureView> Texture::createView(
    PixelFormat format,
    uint32_t    arrayLayerCount,
    Aspect      aspect,
    uint32_t    baseArrayLayer,
    uint32_t    baseMipLevel,
    TextureType dimension)
{
    if (!native) return nullptr;
    auto* h   = static_cast<TextureHandle*>(native);
    auto* dev = h->device;

    DXGI_FORMAT fmt = toDXGIFormat(format == PixelFormat::invalid ? h->format : format);
    if (fmt == DXGI_FORMAT_UNKNOWN) return nullptr;

    bool isDepth = isDepthFormat(h->format);

    auto* vh = new TextureViewHandle();
    vh->format = fmt;

    if (isDepth) {
        // DSV — returned as cpu-only handle in the view
        vh->viewType = ViewType::DSV;
        vh->cpuHandle = h->dsvHandle;
        // no GPU handle for DSV
    } else {
        // SRV — allocate a shader-visible slot; we don't have access to DeviceData here,
        // but TextureHandle::device lets us create an SRV on a shared heap.
        // For simplicity the view gets the pre-allocated RTV handle as cpuHandle
        // and the SRV is created separately.
        //
        // In practice callers use the TextureView for render pass attachments (RTV)
        // or as a shader resource. We store the RTV handle (already allocated on
        // createTexture) and mark the type accordingly.
        if (h->rtvHandle.ptr != 0) {
            vh->viewType  = ViewType::RTV;
            vh->cpuHandle = h->rtvHandle;
        } else {
            // Regular texture — create an SRV directly on the device's SRV heap.
            // We need the DeviceData for this; since we don't store it in TextureHandle,
            // create a temporary non-shader-visible heap descriptor.
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                  = fmt;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

            uint32_t layers = (arrayLayerCount == static_cast<uint32_t>(-1))
                ? std::max(h->depthOrLayers - baseArrayLayer, 1u)
                : arrayLayerCount;

            switch (dimension) {
                case TextureType::tt1d:
                    srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_TEXTURE1D;
                    srvDesc.Texture1D.MostDetailedMip  = baseMipLevel;
                    srvDesc.Texture1D.MipLevels        = h->mipLevels - baseMipLevel;
                    break;
                case TextureType::tt3d:
                    srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_TEXTURE3D;
                    srvDesc.Texture3D.MostDetailedMip  = baseMipLevel;
                    srvDesc.Texture3D.MipLevels        = h->mipLevels - baseMipLevel;
                    break;
                case TextureType::ttCube:
                    srvDesc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURECUBE;
                    srvDesc.TextureCube.MostDetailedMip    = baseMipLevel;
                    srvDesc.TextureCube.MipLevels          = h->mipLevels - baseMipLevel;
                    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
                    break;
                case TextureType::ttCubeArray:
                    srvDesc.ViewDimension                      = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
                    srvDesc.TextureCubeArray.MostDetailedMip   = baseMipLevel;
                    srvDesc.TextureCubeArray.MipLevels         = h->mipLevels - baseMipLevel;
                    srvDesc.TextureCubeArray.First2DArrayFace  = baseArrayLayer;
                    srvDesc.TextureCubeArray.NumCubes          = layers / 6;
                    srvDesc.TextureCubeArray.ResourceMinLODClamp = 0.0f;
                    break;
                default:
                    if (layers > 1) {
                        srvDesc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                        srvDesc.Texture2DArray.MostDetailedMip = baseMipLevel;
                        srvDesc.Texture2DArray.MipLevels       = h->mipLevels - baseMipLevel;
                        srvDesc.Texture2DArray.FirstArraySlice = baseArrayLayer;
                        srvDesc.Texture2DArray.ArraySize       = layers;
                    } else {
                        srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srvDesc.Texture2D.MostDetailedMip = baseMipLevel;
                        srvDesc.Texture2D.MipLevels       = h->mipLevels - baseMipLevel;
                    }
                    break;
            }
            vh->viewType = ViewType::SRV;
            // Use the pre-baked SRV handle created in createTexture (via DeviceData).
            // If the texture was created without textureBinding usage the handle will be
            // zero, which callers must handle.
            if (h->srvCpuHandle.ptr != 0) {
                vh->cpuHandle = h->srvCpuHandle;
            } else if (h->deviceData) {
                // Fallback: allocate a new SRV slot now (e.g. for textures created
                // without textureBinding but used as shader resources anyway).
                auto* d = h->deviceData;
                vh->cpuHandle = d->allocSrvCpu();
                h->device->CreateShaderResourceView(h->resource, &srvDesc, vh->cpuHandle);
            }
        }
    }

    return std::shared_ptr<TextureView>(new TextureView(vh));
}

bool Texture::upload(uint64_t /*offset*/, uint64_t length, void* data) {
    if (!native || !data || length == 0) return false;
    auto* h   = static_cast<TextureHandle*>(native);
    auto* dev = h->device;
    if (!dev || !h->queue) return false;

    // Query the actual footprint required by D3D12 for subresource 0
    D3D12_RESOURCE_DESC texDesc = h->resource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT64 totalBytes = 0;
    dev->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);
    if (totalBytes == 0) return false;

    // Create an upload buffer large enough for the full footprint
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = (totalBytes + 255) & ~255ULL;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* uploadBuf = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf))))
        return false;

    // Map and copy source data row-by-row respecting D3D12 row pitch alignment
    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    if (FAILED(uploadBuf->Map(0, &readRange, &mapped))) {
        uploadBuf->Release(); return false;
    }
    // Infer bytes-per-pixel from footprint width vs row pitch
    UINT srcBytesPerRow = static_cast<UINT>(length / (h->height > 0 ? h->height : 1));
    for (UINT row = 0; row < h->height; ++row) {
        std::memcpy(
            static_cast<uint8_t*>(mapped) + footprint.Footprint.RowPitch * row,
            static_cast<const uint8_t*>(data) + srcBytesPerRow * row,
            (std::min)(srcBytesPerRow, footprint.Footprint.RowPitch));
    }
    uploadBuf->Unmap(0, nullptr);

    // Create a one-shot command allocator + list
    ID3D12CommandAllocator*    alloc = nullptr;
    ID3D12GraphicsCommandList* list  = nullptr;
    if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&alloc)))) {
        uploadBuf->Release(); return false;
    }
    if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       alloc, nullptr, IID_PPV_ARGS(&list)))) {
        alloc->Release(); uploadBuf->Release(); return false;
    }

    // Transition texture COMMON → COPY_DEST
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = h->resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource        = h->resource;
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource       = uploadBuf;
    src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    // Transition COPY_DEST → COMMON
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
    list->ResourceBarrier(1, &barrier);

    if (FAILED(list->Close())) {
        list->Release(); alloc->Release(); uploadBuf->Release(); return false;
    }

    ID3D12CommandList* lists[] = { list };
    h->queue->ExecuteCommandLists(1, lists);

    // Synchronously wait for the GPU
    ID3D12Fence* fence = nullptr;
    dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    h->queue->Signal(fence, 1);
    if (fence->GetCompletedValue() < 1) {
        HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        fence->SetEventOnCompletion(1, event);
        WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
    }
    fence->Release();
    list->Release();
    alloc->Release();
    uploadBuf->Release();
    return true;
}

uint32_t    Texture::getDepthOrarrayLayers() {
    if (!native) return 1;
    return static_cast<TextureHandle*>(native)->depthOrLayers;
}
TextureType Texture::getDimension() {
    if (!native) return TextureType::tt2d;
    return static_cast<TextureHandle*>(native)->dimension;
}
PixelFormat Texture::getFormat() {
    if (!native) return PixelFormat::invalid;
    return static_cast<TextureHandle*>(native)->format;
}
uint32_t Texture::getWidth() {
    if (!native) return 0;
    return static_cast<TextureHandle*>(native)->width;
}
uint32_t Texture::getHeight() {
    if (!native) return 0;
    return static_cast<TextureHandle*>(native)->height;
}
uint32_t Texture::getMipLevelCount() {
    if (!native) return 0;
    return static_cast<TextureHandle*>(native)->mipLevels;
}
uint32_t Texture::getSampleCount() {
    if (!native) return 1;
    return static_cast<TextureHandle*>(native)->sampleCount;
}
TextureUsage Texture::getUsage() {
    if (!native) return static_cast<TextureUsage>(0);
    return static_cast<TextureHandle*>(native)->usage;
}

bool Texture::download(uint32_t mipLevel, uint32_t arrayLayer, void *data, uint64_t length) {
    if (!native || !data || length == 0) return false;
    auto* h = static_cast<TextureHandle*>(native);
    if (!h->device || !h->queue) return false;

    // Get texture description and calculate footprint first to determine required buffer size
    D3D12_RESOURCE_DESC texDesc = h->resource->GetDesc();
    UINT subresource = CalcSubresource(mipLevel, arrayLayer, 0,
        texDesc.MipLevels, texDesc.DepthOrArraySize);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT numRows;
    UINT64 rowSizeInBytes;
    UINT64 totalBytes;
    h->device->GetCopyableFootprints(&texDesc, subresource, 1, 0,
        &footprint, &numRows, &rowSizeInBytes, &totalBytes);

    // Ensure the provided length is sufficient
    if (length < rowSizeInBytes * numRows) {
        // The user-provided buffer might be tightly packed, but we need to handle row pitch
        // Continue anyway and copy only what fits
    }

    // Create a readback buffer (must be large enough for the footprint)
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = totalBytes;  // Use the actual required size from GetCopyableFootprints
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* readbackBuf = nullptr;
    if (FAILED(h->device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&readbackBuf)))) return false;

    // Create a command allocator and list
    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    if (FAILED(h->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(&alloc)))) {
        readbackBuf->Release();
        return false;
    }
    if (FAILED(h->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             alloc, nullptr, IID_PPV_ARGS(&list)))) {
        alloc->Release();
        readbackBuf->Release();
        return false;
    }

    // Transition texture to COPY_SOURCE
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = h->resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = subresource;
    list->ResourceBarrier(1, &barrier);

    // Copy texture to buffer
    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource        = h->resource;
    srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = subresource;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource       = readbackBuf;
    dstLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint = footprint;

    list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    // Transition texture back to COMMON
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
    list->ResourceBarrier(1, &barrier);

    if (FAILED(list->Close())) {
        list->Release();
        alloc->Release();
        readbackBuf->Release();
        return false;
    }

    // Execute and wait
    ID3D12CommandList* lists[] = { list };
    h->queue->ExecuteCommandLists(1, lists);

    ID3D12Fence* fence = nullptr;
    h->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    h->queue->Signal(fence, 1);
    if (fence->GetCompletedValue() < 1) {
        HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        fence->SetEventOnCompletion(1, event);
        WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
    }

    // Map and copy data
    bool result = false;
    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(length) };
    if (SUCCEEDED(readbackBuf->Map(0, &readRange, &mapped))) {
        // Handle row pitch differences
        if (footprint.Footprint.RowPitch == rowSizeInBytes) {
            // Tightly packed, copy all at once
            memcpy(data, mapped, length);
        } else {
            // Need to copy row by row
            uint8_t* dst = static_cast<uint8_t*>(data);
            uint8_t* src = static_cast<uint8_t*>(mapped);
            for (UINT row = 0; row < numRows; ++row) {
                memcpy(dst + row * rowSizeInBytes,
                       src + row * footprint.Footprint.RowPitch,
                       static_cast<size_t>(rowSizeInBytes));
            }
        }
        readbackBuf->Unmap(0, nullptr);
        result = true;
    }

    fence->Release();
    list->Release();
    alloc->Release();
    readbackBuf->Release();
    return result;
}
