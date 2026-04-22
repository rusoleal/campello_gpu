#pragma once
#include <cstdint>

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
    };

} // namespace systems::leal::campello_gpu
