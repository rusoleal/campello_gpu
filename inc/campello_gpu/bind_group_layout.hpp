#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief Defines the structure of a group of shader resource bindings.
     *
     * A `BindGroupLayout` acts as a template that describes which resources (buffers,
     * textures, samplers) are bound at which shader binding slots, their types, and
     * which shader stages can access them.
     *
     * A layout must be created before creating a `BindGroup` that conforms to it.
     * On Metal, resource binding is implicit and this type is a no-op placeholder.
     * On Vulkan, this maps to `VkDescriptorSetLayout`.
     *
     * Bind group layouts are created by `Device::createBindGroupLayout()`.
     *
     * @code
     * BindGroupLayoutDescriptor desc{};
     * // desc.entries = { ... };  // define binding slots
     * auto layout = device->createBindGroupLayout(desc);
     * @endcode
     */
    class BindGroupLayout {
    private:
        friend class Device;
        void *native;
        BindGroupLayout(void *pd);

    public:
        ~BindGroupLayout();
    };

}
