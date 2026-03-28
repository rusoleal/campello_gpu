#pragma once

#include <stdint.h>

namespace systems::leal::campello_gpu
{
    class Device;

    /**
     * @brief A GPU-side memory buffer.
     *
     * Buffers hold raw data on the GPU — vertex data, index data, uniform constants,
     * storage data, etc. The intended usage is declared at creation time via `BufferUsage`
     * and cannot be changed afterwards.
     *
     * Buffers are created exclusively by `Device::createBuffer()`.
     *
     * @code
     * // Vertex buffer pre-filled with data
     * float vertices[] = { 0.f, 0.5f,  -0.5f, -0.5f,  0.5f, -0.5f };
     * auto vb = device->createBuffer(sizeof(vertices), BufferUsage::vertex, vertices);
     *
     * // Uniform buffer, filled later via upload()
     * auto ub = device->createBuffer(sizeof(Uniforms), BufferUsage::uniform);
     * ub->upload(0, sizeof(Uniforms), &uniforms);
     * @endcode
     */
    class Buffer
    {
        friend class Device;
        friend class CommandEncoder;
        friend class RenderPassEncoder;
        friend class ComputePassEncoder;
        void *native;

        Buffer(void *pd);

    public:
        /**
         * @brief Returns the size of this buffer in bytes.
         * @return Allocated byte length of the buffer.
         */
        uint64_t getLength();

        /**
         * @brief Uploads CPU data into the buffer.
         *
         * Writes `length` bytes from `data` into the buffer starting at `offset`.
         * The buffer must have been created with host-visible storage (the default
         * on most backends for non-private buffers).
         *
         * @param offset  Byte offset within the buffer at which to begin writing.
         * @param length  Number of bytes to write.
         * @param data    Pointer to the source data.
         * @return `true` on success, `false` if the write could not be completed.
         */
        bool upload(uint64_t offset, uint64_t length, void *data);

        /**
         * @brief Downloads GPU buffer data to CPU memory.
         *
         * Copies `length` bytes from the GPU buffer starting at `offset` into the
         * provided CPU memory pointer. The buffer must have been created with
         * `BufferUsage::copyDst` and be in a state accessible for readback.
         *
         * For textures, use `CommandEncoder::copyTextureToBuffer()` first to copy
         * texture data into a buffer, then call `download()` on that buffer.
         *
         * @param offset  Byte offset within the buffer at which to begin reading.
         * @param length  Number of bytes to read.
         * @param data    Pointer to the destination CPU memory (must be at least `length` bytes).
         * @return `true` on success, `false` if the read could not be completed.
         */
        bool download(uint64_t offset, uint64_t length, void *data);

        ~Buffer();
    };

}
