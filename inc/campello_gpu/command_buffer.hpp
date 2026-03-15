#pragma once

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
    };

}
