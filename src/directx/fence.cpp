#include "common.hpp"
#include <campello_gpu/fence.hpp>
#include <windows.h>
#include <cstdio>

using namespace systems::leal::campello_gpu;

Fence::Fence(void *pd) : native(pd) {}

Fence::~Fence() {
    if (native != nullptr) {
        auto *data = static_cast<DirectXFenceData *>(native);
        if (data->fenceEvent) CloseHandle(data->fenceEvent);
        if (data->fence) data->fence->Release();
        delete data;
    }
}

bool Fence::wait(uint64_t timeoutNs) {
    if (native == nullptr) return true;
    auto *data = static_cast<DirectXFenceData *>(native);

    // Unlike Vulkan's vkWaitForFences/vkGetFenceStatus (which return a
    // distinct VK_ERROR_DEVICE_LOST that has to be special-cased), a
    // device-removed fence here reports GetCompletedValue() == UINT64_MAX,
    // which already satisfies ">= data->value" -- so this already returns
    // true (done waiting; success is not implied, see didFail()) without
    // needing a separate check.
    if (data->fence->GetCompletedValue() >= data->value) return true;

    DWORD timeoutMs = (timeoutNs == UINT64_MAX)
                          ? INFINITE
                          : static_cast<DWORD>(timeoutNs / 1'000'000);

    data->fence->SetEventOnCompletion(data->value, data->fenceEvent);
    DWORD result = WaitForSingleObject(data->fenceEvent, timeoutMs);
    return (result == WAIT_OBJECT_0);
}

bool Fence::isSignaled() const {
    if (native == nullptr) return true;
    auto *data = static_cast<DirectXFenceData *>(native);
    // Also already correct on device removal -- see wait()'s comment above.
    return data->fence->GetCompletedValue() >= data->value;
}

bool Fence::didFail() const {
    if (native == nullptr) return false;
    auto *data = static_cast<DirectXFenceData *>(native);
    // D3D12 has no per-submission failure status the way Metal's
    // MTLCommandBuffer::status() does -- a removed/hung/reset device reports
    // GetCompletedValue() == UINT64_MAX regardless of what value this fence
    // was actually signaled to (the documented way to detect device removal
    // without querying GetDeviceRemovedReason() on every call). It means the
    // whole ID3D12Device (not just this fence's submission) is gone and must
    // be recreated.
    return data->fence->GetCompletedValue() == UINT64_MAX;
}

std::string Fence::failureReason() const {
    if (!didFail()) return {};
    auto *data = static_cast<DirectXFenceData *>(native);
    if (!data->device) return "device removed (reason unavailable: no device pointer)";

    HRESULT reason = data->device->GetDeviceRemovedReason();
    switch (reason) {
        case DXGI_ERROR_DEVICE_HUNG:
            return "DXGI_ERROR_DEVICE_HUNG: the GPU hung due to an invalid command stream; the driver reset the device";
        case DXGI_ERROR_DEVICE_REMOVED:
            return "DXGI_ERROR_DEVICE_REMOVED: the GPU was physically removed, crashed, or its driver was updated/reinstalled";
        case DXGI_ERROR_DEVICE_RESET:
            return "DXGI_ERROR_DEVICE_RESET: the GPU reset due to a badly formed command";
        case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
            return "DXGI_ERROR_DRIVER_INTERNAL_ERROR: the driver encountered an internal error";
        case DXGI_ERROR_INVALID_CALL:
            return "DXGI_ERROR_INVALID_CALL: the application made an invalid D3D12 API call";
        default: {
            char buf[64];
            snprintf(buf, sizeof(buf), "device removed (HRESULT 0x%08lX)", (unsigned long)reason);
            return buf;
        }
    }
}
