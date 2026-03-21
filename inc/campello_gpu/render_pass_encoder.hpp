#pragma once

#include <memory>
#include <vector>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/constants/index_format.hpp>
#include <campello_gpu/render_pipeline.hpp>

namespace systems::leal::campello_gpu
{
    /**
     * @brief Records draw commands inside a render pass.
     *
     * A `RenderPassEncoder` is obtained from `CommandEncoder::beginRenderPass()`.
     * Use it to set the pipeline state, bind vertex/index buffers, configure the
     * viewport and scissor, and issue draw calls. Call `end()` when all draw commands
     * for the pass have been recorded.
     *
     * **Usage pattern:**
     * @code
     * auto rpe = encoder->beginRenderPass(rpDesc);
     * rpe->setPipeline(pipeline);
     * rpe->setVertexBuffer(0, vertexBuffer, 0);
     * rpe->setViewport(0, 0, width, height, 0, 1);
     * rpe->draw(vertexCount, 1, 0, 0);
     * rpe->end();
     * @endcode
     */
    class RenderPassEncoder
    {
    private:
        friend class CommandEncoder;
        void *native;
        RenderPassEncoder(void *pd);
    public:
        ~RenderPassEncoder();

        /**
         * @brief Begins an occlusion query in the given slot.
         *
         * Primitives drawn after this call until `endOcclusionQuery()` contribute to
         * the sample count recorded in `queryIndex` of the pass's occlusion query set.
         *
         * @param queryIndex Slot index in the `QuerySet` to write into.
         */
        void beginOcclusionQuery(uint32_t queryIndex);

        /**
         * @brief Issues a non-indexed draw call.
         *
         * @param vertexCount   Number of vertices to draw per instance.
         * @param instanceCount Number of instances to draw (default: 1).
         * @param firstVertex   Index of the first vertex in the vertex buffer (default: 0).
         * @param firstInstance Instance ID of the first instance (default: 0).
         */
        void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                  uint32_t firstVertex = 0, uint32_t firstInstance = 0);

        /**
         * @brief Issues an indexed draw call using the currently bound index buffer.
         *
         * @param indexCount    Number of indices to read per instance.
         * @param instanceCount Number of instances to draw (default: 1).
         * @param firstVertex   Offset added to each index value before fetching vertices (default: 0).
         * @param baseVertex    Constant added to each index value (default: 0).
         * @param firstInstance Instance ID of the first instance (default: 0).
         */
        void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                         uint32_t firstVertex = 0, uint32_t baseVertex = 0,
                         uint32_t firstInstance = 0);

        /**
         * @brief Issues a non-indexed draw call with parameters sourced from a buffer.
         *
         * The GPU reads draw arguments from `indirectBuffer` at `indirectOffset`.
         *
         * @param indirectBuffer Buffer containing the draw arguments.
         * @param indirectOffset Byte offset within the buffer to the argument struct.
         */
        void drawIndirect(std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset);

        /**
         * @brief Issues an indexed draw call with parameters sourced from a buffer.
         *
         * Uses the currently bound index buffer. The GPU reads arguments from
         * `indirectBuffer` at `indirectOffset`.
         *
         * @param indirectBuffer Buffer containing the indexed draw arguments.
         * @param indirectOffset Byte offset within the buffer to the argument struct.
         */
        void drawIndexedIndirect(std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset);

        /**
         * @brief Ends the render pass.
         *
         * Must be called exactly once after all draw commands have been recorded.
         * After this call the encoder must not be used again.
         */
        void end();

        /** @brief Ends the current occlusion query started by `beginOcclusionQuery()`. */
        void endOcclusionQuery();

        /**
         * @brief Binds an index buffer for subsequent indexed draw calls.
         *
         * @param buffer      Buffer containing index data.
         * @param indexFormat Element type: `IndexFormat::uint16` or `IndexFormat::uint32`.
         * @param offset      Byte offset within the buffer to the first index (default: 0).
         * @param size        Byte length of the index region, or -1 for the full buffer (default: -1).
         */
        void setIndexBuffer(std::shared_ptr<Buffer> buffer, IndexFormat indexFormat,
                            uint64_t offset = 0, int64_t size = -1);

        /**
         * @brief Binds a `BindGroup` to the given index for subsequent draw calls.
         *
         * On Metal, iterates the group's entries and binds each resource directly on
         * the encoder (textures and samplers to both vertex and fragment stages,
         * buffers to both stages).
         *
         * @param index                  Binding group index in the pipeline layout.
         * @param bindGroup              The resource group to bind.
         * @param dynamicOffsets         Dynamic offsets for dynamic-offset bindings.
         * @param dynamicOffsetsStart    First element in `dynamicOffsets` to use.
         * @param dynamicOffsetsLength   Number of dynamic offsets to apply.
         */
        void setBindGroup(uint32_t index, std::shared_ptr<BindGroup> bindGroup,
                          const std::vector<uint32_t> &dynamicOffsets = {},
                          uint64_t dynamicOffsetsStart = 0,
                          uint64_t dynamicOffsetsLength = 0);

        /**
         * @brief Sets the render pipeline for subsequent draw calls.
         *
         * Must be called before any draw call. The pipeline encodes the shaders,
         * vertex layout, blend state, and output formats used for rendering.
         *
         * @param pipeline The compiled render pipeline to use.
         */
        void setPipeline(std::shared_ptr<RenderPipeline> pipeline);

        /**
         * @brief Sets the scissor rectangle, clipping all rasterization outside it.
         *
         * @param x      Left edge of the scissor rect in pixels.
         * @param y      Top edge of the scissor rect in pixels.
         * @param width  Width of the scissor rect in pixels.
         * @param height Height of the scissor rect in pixels.
         */
        void setScissorRect(float x, float y, float width, float height);

        /**
         * @brief Sets the stencil reference value used in stencil comparison operations.
         *
         * @param reference The reference value compared against the stencil buffer.
         */
        void setStencilReference(uint32_t reference);

        /**
         * @brief Binds a vertex buffer to the given slot.
         *
         * @param slot   Vertex buffer slot index (matches the layout in the pipeline).
         * @param buffer The buffer containing vertex data.
         * @param offset Byte offset within the buffer to the first vertex (default: 0).
         * @param size   Byte length of the vertex region, or -1 for the full buffer (default: -1).
         */
        void setVertexBuffer(uint32_t slot, std::shared_ptr<Buffer> buffer,
                             uint64_t offset = 0, int64_t size = -1);

        /**
         * @brief Sets the viewport transform applied to clip-space coordinates.
         *
         * @param x        Left edge of the viewport in pixels.
         * @param y        Top edge of the viewport in pixels.
         * @param width    Width of the viewport in pixels.
         * @param height   Height of the viewport in pixels.
         * @param minDepth Minimum depth value (typically 0.0).
         * @param maxDepth Maximum depth value (typically 1.0).
         */
        void setViewport(float x, float y, float width, float height,
                         float minDepth, float maxDepth);
    };

}
