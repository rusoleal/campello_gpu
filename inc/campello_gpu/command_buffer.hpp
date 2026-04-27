#pragma once

#include <cstdint>
#include <functional>

namespace systems::leal::campello_gpu
{
    class Device;

    /**
     * @brief A recorded, ready-to-submit command buffer.
     *
     * A `CommandBuffer` holds the complete sequence of GPU commands recorded by a
     * `CommandEncoder`. Once obtained from `CommandEncoder::finish()`, submit it to
     * the GPU with `Device::submit()`.
     *
     * A command buffer can only be submitted once. After submission, the GPU owns it
     * and the object should be released.
     *
     * @code
     * auto encoder    = device->createCommandEncoder();
     * auto renderPass = encoder->beginRenderPass(rpDesc);
     * // ... record draw calls ...
     * renderPass->end();
     * auto cmdBuffer = encoder->finish();
     * device->submit(cmdBuffer);
     * @endcode
     */
    class CommandBuffer
    {
        friend class Device;
        friend class CommandEncoder;
        void *native;
        CommandBuffer(void *pd);
    public:
        ~CommandBuffer();
        
        /**
         * @brief Returns the GPU execution time in nanoseconds.
         *
         * This is only valid after the command buffer has completed execution
         * on the GPU. Returns 0 if timing is not available.
         *
         * @return GPU execution time in nanoseconds.
         */
        uint64_t getGPUExecutionTime();

        /**
         * @brief Asynchronously returns the GPU execution time in nanoseconds.
         *
         * Non-blocking. The callback is invoked on the main thread when the
         * timing data is available. On WebGPU/WASM, this avoids `emscripten_sleep`
         * polling and is safe to call from the browser main thread.
         *
         * @param callback Invoked with the GPU execution time in nanoseconds.
         *                 A value of 0 means timing is not available.
         */
        void getGPUExecutionTimeAsync(std::function<void(uint64_t)> callback);
    };

}
