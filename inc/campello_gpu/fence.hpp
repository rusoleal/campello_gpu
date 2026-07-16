#pragma once
#include <cstdint>
#include <string>

namespace systems::leal::campello_gpu
{
    /**
     * @brief Binary GPU completion fence.
     *
     * A Fence is signaled by the GPU after a specific command buffer finishes
     * executing. The CPU can block on it (wait) or poll (isSignaled) to know
     * when it is safe to reuse the resources associated with that submission.
     *
     * Typical frame-in-flight usage:
     * @code
     * auto fence = device->createFence();
     * device->submit(cmdBuffer, fence);   // non-blocking
     * // ... do other CPU work ...
     * fence->wait();                      // block until GPU done
     * @endcode
     */
    class Fence
    {
        friend class Device;
        void *native;
        Fence(void *pd);
    public:
        ~Fence();
        /**
         * @brief Block until the GPU signals this fence.
         * @param timeoutNs Maximum time to wait in nanoseconds.
         *                  Default (UINT64_MAX) waits forever.
         * @return true if the fence was signaled, false if the timeout expired.
         */
        bool wait(uint64_t timeoutNs = UINT64_MAX);
        /**
         * @brief Poll whether the fence has been signaled without blocking.
         * @return true if the GPU has completed the associated work.
         */
        bool isSignaled() const;
        /**
         * @brief Whether the submission this fence tracks failed on the GPU
         * (e.g. a driver-level timeout, page fault, or device removal) rather
         * than completing normally. Only meaningful after wait()/isSignaled()
         * report the fence as signaled -- a submission that failed still
         * signals its fence (the GPU driver invokes the completion callback
         * either way), so callers that care whether their dispatch actually
         * succeeded must check this explicitly; wait() returning does not by
         * itself mean the destination buffers hold this submission's output
         * rather than stale data left over from a prior one.
         *
         * Granularity differs by backend:
         * - Metal detects failure of *this specific* command buffer (via
         *   `MTLCommandBuffer::status()`) while the device and other
         *   in-flight/future submissions keep working normally.
         * - Vulkan and DirectX have no equivalent per-submission status; they
         *   detect whole-*device* loss instead (`VK_ERROR_DEVICE_LOST`, and
         *   D3D12's `GetDeviceRemovedReason()` respectively). A true result on
         *   these backends means the entire device -- not just this fence's
         *   submission -- is gone and must be destroyed and recreated; every
         *   other fence/resource on that device should be treated as failed
         *   too, not just the one you happened to check.
         * - WebGPU does not yet implement failure detection and always
         *   reports false here.
         * @return true if the tracked submission (Metal) or its device
         *         (Vulkan/DirectX) failed.
         */
        bool didFail() const;
        /**
         * @brief Human-readable reason for `didFail()`, e.g. the underlying
         * platform error's description ("Caused GPU Timeout Error ...", or a
         * DXGI/Vulkan device-lost error name on backends that only detect
         * whole-device loss -- see `didFail()`'s doc comment for the
         * per-backend granularity difference).
         * Empty if `didFail()` is false, or on backends that don't implement
         * failure detection.
         * @return the failure description, or an empty string.
         */
        std::string failureReason() const;
    };

} // namespace systems::leal::campello_gpu
