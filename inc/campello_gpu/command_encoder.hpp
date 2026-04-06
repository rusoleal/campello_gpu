#pragma once

#include <memory>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/types/offset_3d.hpp>
#include <campello_gpu/types/extent_3d.hpp>

namespace systems::leal::campello_gpu
{
    class Device;

    /**
     * @brief Records a sequence of GPU commands for later submission.
     *
     * A `CommandEncoder` is the primary object for building GPU work. Commands are
     * recorded into it — render passes, compute passes, buffer copies, clears — and
     * then finalized into a `CommandBuffer` via `finish()`. That buffer is submitted
     * to the GPU with `Device::submit()`.
     *
     * Obtain a `CommandEncoder` from `Device::createCommandEncoder()`. A single encoder
     * records commands for one `CommandBuffer`; create a new one each frame.
     *
     * **Typical frame loop:**
     * @code
     * auto encoder    = device->createCommandEncoder();
     *
     * auto renderPass = encoder->beginRenderPass(rpDesc);
     * renderPass->setPipeline(pipeline);
     * renderPass->setVertexBuffer(0, vertexBuffer, 0);
     * renderPass->draw(3, 1, 0, 0);
     * renderPass->end();
     *
     * auto cmdBuffer = encoder->finish();
     * device->submit(cmdBuffer);
     * @endcode
     */
    class CommandEncoder
    {
        friend class Device;
        void *native;

        CommandEncoder(void *pd);
    public:

        ~CommandEncoder();

        /**
         * @brief Begins a compute pass and returns an encoder for it.
         *
         * All compute dispatches must happen inside a compute pass. Only one pass
         * (render or compute) may be active on an encoder at a time.
         *
         * @return A `ComputePassEncoder` for recording compute commands.
         */
        std::shared_ptr<ComputePassEncoder> beginComputePass();

        /**
         * @brief Begins a render pass and returns an encoder for it.
         *
         * The `descriptor` specifies the color and depth/stencil attachments, their
         * load/store actions, and clear values. Only one pass may be active at a time.
         *
         * @param descriptor Configuration for the render pass attachments.
         * @return A `RenderPassEncoder` for recording draw commands.
         */
        std::shared_ptr<RenderPassEncoder> beginRenderPass(const BeginRenderPassDescriptor &descriptor);

        /**
         * @brief Fills a region of a buffer with zeros.
         *
         * @param buffer The target buffer.
         * @param offset Byte offset within the buffer at which to start clearing.
         * @param size   Number of bytes to clear.
         */
        void clearBuffer(std::shared_ptr<Buffer> buffer, uint64_t offset, uint64_t size);

        /**
         * @brief Copies a region of bytes from one buffer to another.
         *
         * @param source             Source buffer.
         * @param sourceOffset       Byte offset within the source.
         * @param destination        Destination buffer.
         * @param destinationOffset  Byte offset within the destination.
         * @param size               Number of bytes to copy.
         */
        void copyBufferToBuffer(std::shared_ptr<Buffer> source, uint64_t sourceOffset,
                                std::shared_ptr<Buffer> destination, uint64_t destinationOffset,
                                uint64_t size);

        /**
         * @brief Copies data from a buffer into a texture subresource.
         *
         * The buffer must have been created with `BufferUsage::copySrc`.
         * The texture must have been created with `TextureUsage::copyDst`.
         *
         * @param source            Source buffer.
         * @param sourceOffset      Byte offset within the buffer to the first texel.
         * @param bytesPerRow       Bytes per row in the source buffer (row pitch).
         *                          Pass 0 for tightly-packed data.
         * @param destination       Destination texture.
         * @param mipLevel          Mip level of the destination to write into.
         * @param arrayLayer        Array layer (for array textures) or depth slice (for 3D) to write into.
         */
        void copyBufferToTexture(
            std::shared_ptr<Buffer>  source,
            uint64_t                 sourceOffset,
            uint64_t                 bytesPerRow,
            std::shared_ptr<Texture> destination,
            uint32_t                 mipLevel,
            uint32_t                 arrayLayer);

        /**
         * @brief Copies texture data into a buffer.
         *
         * Copies the contents of a texture subresource into a buffer. This is
         * typically used for readback (GPU → CPU) operations. After the copy,
         * the destination buffer can be mapped and its contents read on the CPU.
         *
         * The buffer must have been created with `BufferUsage::copyDst` usage.
         * The texture must have been created with `TextureUsage::copySrc` usage.
         *
         * @param source        Source texture to copy from.
         * @param mipLevel      Mip level of the texture to copy.
         * @param arrayLayer    Array layer (for array textures) or depth slice (for 3D) to copy.
         * @param destination   Destination buffer.
         * @param destinationOffset Byte offset within the destination buffer.
         * @param bytesPerRow   Number of bytes per row in the destination buffer (row pitch).
         */
        void copyTextureToBuffer(
            std::shared_ptr<Texture> source,
            uint32_t mipLevel,
            uint32_t arrayLayer,
            std::shared_ptr<Buffer> destination,
            uint64_t destinationOffset,
            uint64_t bytesPerRow);

        /**
         * @brief Copies a region of one texture into another.
         *
         * Both textures must be the same pixel format. The source must have been
         * created with `TextureUsage::copySrc` (or be a render-target texture that
         * supports blit reads on the platform), and the destination must have
         * `TextureUsage::copyDst`.
         *
         * @param source             Source texture.
         * @param sourceOffset       Offset within the source texture (in pixels).
         * @param destination        Destination texture.
         * @param destinationOffset  Offset within the destination texture (in pixels).
         * @param extent             Size of the region to copy (in pixels).
         */
        void copyTextureToTexture(
            std::shared_ptr<Texture> source,
            const Offset3D& sourceOffset,
            std::shared_ptr<Texture> destination,
            const Offset3D& destinationOffset,
            const Extent3D& extent);

        /**
         * @brief Finalizes command recording and returns the completed command buffer.
         *
         * After calling `finish()` the encoder must not be used again. The returned
         * `CommandBuffer` is ready to be submitted with `Device::submit()`.
         *
         * @return A `CommandBuffer` containing all recorded commands.
         */
        std::shared_ptr<CommandBuffer> finish();

        /**
         * @brief Copies query results from a `QuerySet` into a `Buffer`.
         *
         * Resolves `queryCount` query slots starting at `firstQuery` from `querySet`
         * into `destination` at `destinationOffset`. The resolved data is 8 bytes
         * (uint64) per query slot.
         *
         * @param querySet            The query set to read from.
         * @param firstQuery          Index of the first query slot to resolve.
         * @param queryCount          Number of query slots to resolve.
         * @param destination         Buffer that receives the resolved values.
         * @param destinationOffset   Byte offset within the destination buffer.
         */
        void resolveQuerySet(std::shared_ptr<QuerySet> querySet, uint32_t firstQuery,
                             uint32_t queryCount, std::shared_ptr<Buffer> destination,
                             uint64_t destinationOffset);

        /**
         * @brief Records a GPU timestamp into a `QuerySet` slot.
         *
         * The GPU writes its current clock value into slot `queryIndex` of `querySet`
         * when this command executes. Resolve the value to a buffer later with
         * `resolveQuerySet()`.
         *
         * @param querySet   The query set that receives the timestamp.
         * @param queryIndex Index of the slot to write into.
         */
        void writeTimestamp(std::shared_ptr<QuerySet> querySet, uint32_t queryIndex);
    };

}
