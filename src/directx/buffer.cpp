#define NOMINMAX
#include "common.hpp"
#include <campello_gpu/buffer.hpp>
#include <cstring>

using namespace systems::leal::campello_gpu;

Buffer::Buffer(void* pd) : native(pd) {}

Buffer::~Buffer() {
    if (!native) return;
    auto* h = static_cast<BufferHandle*>(native);
    
    // Phase 2: Update memory tracking
    if (h->deviceData) {
        h->deviceData->bufferCount--;
        h->deviceData->bufferBytes.fetch_sub(h->allocatedSize);
    }
    
    if (h->resource) {
        if (h->mapped) h->resource->Unmap(0, nullptr);
        // Released once the GPU can no longer be using it (DeviceData::deferRelease()).
        if (h->deviceData) h->deviceData->deferRelease(h->resource);
        else h->resource->Release();
    }
    delete h;
}

uint64_t Buffer::getLength() {
    if (!native) return 0;
    return static_cast<BufferHandle*>(native)->size;
}

bool Buffer::upload(uint64_t offset, uint64_t length, void* data) {
    if (!native || !data) return false;
    auto* h = static_cast<BufferHandle*>(native);
    if (offset + length > h->size) return false;
    if (h->mapped) {
        std::memcpy(static_cast<uint8_t*>(h->mapped) + offset, data, length);
        return true;
    }
    // A default-heap (storage) buffer isn't CPU-visible: copy through a
    // staging upload buffer and wait for the copy, as Texture::upload() does.
    // It's in COMMON (Device::createBuffer()), which promotes to COPY_DEST.
    // Before, this returned false, so createBuffer(size, storage, data) made
    // a buffer of zeros.
    if (h->isReadback || !h->device || !h->queue || length == 0) return false;

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = length;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* staging = nullptr;
    if (FAILED(h->device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                   IID_PPV_ARGS(&staging)))) return false;
    void* mapped = nullptr;
    D3D12_RANGE noRead = { 0, 0 };
    if (FAILED(staging->Map(0, &noRead, &mapped))) { staging->Release(); return false; }
    std::memcpy(mapped, data, length);
    staging->Unmap(0, nullptr);

    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    if (FAILED(h->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)))) {
        staging->Release();
        return false;
    }
    if (FAILED(h->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&list)))) {
        alloc->Release();
        staging->Release();
        return false;
    }
    list->CopyBufferRegion(h->resource, offset, staging, 0, length);
    bool ok = SUCCEEDED(list->Close());
    if (ok) {
        ID3D12CommandList* lists[] = { list };
        h->queue->ExecuteCommandLists(1, lists);
        ID3D12Fence* fence = nullptr;
        ok = SUCCEEDED(h->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        if (ok) {
            h->queue->Signal(fence, 1);
            if (fence->GetCompletedValue() < 1) {
                HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                fence->SetEventOnCompletion(1, event);
                WaitForSingleObject(event, INFINITE);
                CloseHandle(event);
            }
            fence->Release();
        }
    }
    list->Release();
    alloc->Release();
    staging->Release();
    return ok;
}

bool Buffer::download(uint64_t offset, uint64_t length, void* data) {
    if (!native || !data) return false;
    auto* h = static_cast<BufferHandle*>(native);
    if (!h->resource) return false;
    if (offset + length > h->size) return false;

    // For readback heaps, map and copy directly
    if (h->isReadback && h->mapped) {
        std::memcpy(data, static_cast<uint8_t*>(h->mapped) + offset, length);
        return true;
    }

    // For upload/default heaps, we need to create a temporary readback buffer
    // and copy data to it first. This requires a command list and queue.
    if (!h->device || !h->queue) return false;

    // Create a readback buffer
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = (length + 255) & ~255ULL;  // align to 256 bytes
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

    // Create a command list to copy the data
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

    // Copy from source buffer to readback buffer
    list->CopyBufferRegion(readbackBuf, 0, h->resource, offset, length);

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
    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(length) };
    if (SUCCEEDED(readbackBuf->Map(0, &readRange, &mapped))) {
        std::memcpy(data, mapped, length);
        readbackBuf->Unmap(0, nullptr);
    }

    fence->Release();
    list->Release();
    alloc->Release();
    readbackBuf->Release();
    return true;
}

void Buffer::downloadAsync(uint64_t offset, uint64_t length, void* data,
                           std::function<void(bool)> callback) {
    callback(download(offset, length, data));
}
