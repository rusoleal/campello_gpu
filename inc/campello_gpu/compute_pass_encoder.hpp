#pragma once

#include <memory>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/bind_group.hpp>

namespace systems::leal::campello_gpu {

    class ComputePassEncoder {
    private:
        void *native;
        ComputePassEncoder(void *pd);
    public:
       ~ComputePassEncoder();
        void dispatchWorkgroups(uint64_t workgroupCountX, uint64_t workgroupCountY=1, uint64_t workgroupCountZ=1);
        void dispatchWorkgroupsIndirect(std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset);
        void end();
        void setBindGroup(uint32_t index, std::shared_ptr<BindGroup> bindGroup, const std::vector<uint32_t> &dynamicOffsets, uint64_t dynamicOffsetsStart, uint64_t dynamicOffsetsLength);
        void setPipeline(std::shared_ptr<ComputePipeline> pipeline);
    };

}