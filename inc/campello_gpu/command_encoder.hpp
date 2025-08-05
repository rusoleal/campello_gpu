#pragma once

#include <memory>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/query_set.hpp>

namespace systems::leal::campello_gpu
{
    class Device;

    class CommandEncoder
    {
        friend class Device;
        void *native;

        CommandEncoder(void *pd);
    public:

        ~CommandEncoder();
        std::shared_ptr<ComputePassEncoder> beginComputePass();
        std::shared_ptr<RenderPassEncoder> beginRenderPass(const BeginRenderPassDescriptor &descriptor);
        void clearBuffer(std::shared_ptr<Buffer> buffer, uint64_t offset, uint64_t size);
        void copyBufferToBuffer(std::shared_ptr<Buffer> source, uint64_t sourceOffset, std::shared_ptr<Buffer> destination, uint64_t destinationOffset, uint64_t size);
        void copyBufferToTexture(); // TODO
        void copyTextureToBuffer(); // TODO
        void copyTextureToTexture(); // TODO
        std::shared_ptr<CommandBuffer> finish();
        void resolveQuerySet(std::shared_ptr<QuerySet> querySet, uint32_t firstQuery, uint32_t queryCount, std::shared_ptr<Buffer> destination, uint64_t destinationOffset);
        void writeTimestamp(std::shared_ptr<QuerySet> querySet, uint32_t queryIndex);

    };

}