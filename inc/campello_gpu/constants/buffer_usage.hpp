#pragma once

namespace systems::leal::campello_gpu
{

    /**
     * @brief Bitmask flags describing how a `Buffer` may be used.
     *
     * Combine flags with bitwise OR when creating a buffer to declare all of its
     * intended uses up front. The runtime may reject buffer bindings that were not
     * declared at creation time.
     *
     * @code
     * // A buffer suitable for uploading and then binding as a vertex buffer:
     * auto buf = device->createBuffer({ size, BufferUsage::copyDst | BufferUsage::vertex });
     * @endcode
     */
    enum class BufferUsage
    {
        copySrc      = 0x0004, ///< Can be used as the source of a copy operation.
        copyDst      = 0x0008, ///< Can be used as the destination of a copy operation.
        index        = 0x0010, ///< Can be bound as an index buffer.
        indirect     = 0x0100, ///< Can be used as the source of an indirect draw/dispatch call.
        mapRead      = 0x0001, ///< Can be mapped for reading by the CPU.
        mapWrite     = 0x0002, ///< Can be mapped for writing by the CPU.
        queryResolve = 0x0200, ///< Can be used as the destination of a query resolve operation.
        storage      = 0x0080, ///< Can be bound as a read/write storage buffer in a shader.
        uniform      = 0x0040, ///< Can be bound as a uniform (constant) buffer in a shader.
        vertex       = 0x0020, ///< Can be bound as a vertex buffer.
        accelerationStructureInput   = 0x0400, ///< Geometry / instance data read during an acceleration-structure build.
        accelerationStructureStorage = 0x0800  ///< Backing GPU memory for a built acceleration structure.
    };

}
