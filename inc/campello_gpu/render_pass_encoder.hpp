#pragma once

#include <memory>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/constants/index_format.hpp>
#include <campello_gpu/render_pipeline.hpp>

namespace systems::leal::campello_gpu
{
    class RenderPassEncoder
    {
    private:
        void *native;
        RenderPassEncoder(void *pd);
    public:
        ~RenderPassEncoder();
        void beginOcclusionQuery(uint32_t queryIndex);
        void draw(uint32_t vertexCount, uint32_t instanceCount=1, uint32_t firstVertex=0, uint32_t firstInstance=0);
        void drawIndexed(uint32_t indexCount, uint32_t instanceCount=1, uint32_t firstVertex=0, uint32_t baseVertex=0, uint32_t firstInstance=0);
        void drawIndirect(std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset);
        void drawIndexedIndirect(std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset);
        void end();
        void endOcclusionQuery();
        //void executeBundles(bundles); // TODO
        //void setBindGroup(uint32_t index, bindGroup, dynamicOffsets, dynamicOffsetsStart, dynamicOffsetsLength);
        //void setBlendConstant(color);
        void setIndexBuffer(std::shared_ptr<Buffer> buffer, IndexFormat indexFormat, uint64_t offset=0, int64_t size=-1);
        void setPipeline(std::shared_ptr<RenderPipeline> pipeline);
        void setScissorRect(float x, float y, float width, float height);
        void setStencilReference(uint32_t reference);
        void setVertexBuffer(uint32_t slot, std::shared_ptr<Buffer> buffer, uint64_t offset=0, int64_t size=-1);
        void setViewport(float x, float y, float width, float height, float minDepth, float maxDepth);
    };

}