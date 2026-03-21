#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief A concrete set of shader resource bindings.
     *
     * A `BindGroup` is a collection of actual GPU resources (buffers, textures,
     * samplers) bound to specific slots, conforming to a `BindGroupLayout`. It is the
     * unit of resource binding passed to the GPU at draw or dispatch time.
     *
     * On Metal, resource binding happens directly on the command encoder and this type
     * is a no-op placeholder. On Vulkan, this maps to a `VkDescriptorSet`.
     *
     * Bind groups are created by `Device::createBindGroup()`.
     *
     * @code
     * BindGroupDescriptor desc{};
     * desc.layout  = bindGroupLayout;
     * desc.entries = {
     *     { 0, BufferBinding{ uniformBuffer, 0, sizeof(Uniforms) } },
     *     { 1, texture },
     *     { 2, sampler },
     * };
     * auto bindGroup = device->createBindGroup(desc);
     * @endcode
     */
    class BindGroup {
    private:
        friend class Device;
        friend class RenderPassEncoder;
        friend class ComputePassEncoder;
        void *native;
        BindGroup(void *pd);
    public:
       ~BindGroup();
    };

}
