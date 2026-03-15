#pragma once

#include <variant>
#include <vector>
#include <memory>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/sampler.hpp>

namespace systems::leal::campello_gpu {

    /**
     * @brief Describes a buffer resource bound to a shader slot.
     *
     * Specifies which region of a `Buffer` is visible to the shader: the full buffer
     * or a sub-range defined by `offset` and `size`.
     */
    struct BufferBinding {
        /** @brief The buffer to bind. */
        std::shared_ptr<Buffer> buffer;

        /** @brief Byte offset within the buffer where the binding begins. */
        uint64_t offset;

        /** @brief Byte length of the bound region. */
        uint64_t size;
    };

    /**
     * @brief Describes a single resource entry within a `BindGroup`.
     *
     * Each entry maps a shader binding index to one concrete resource — either a
     * buffer (with range), a texture, or a sampler.
     */
    struct BindGroupEntryDescriptor {
        /** @brief Shader binding index this entry is bound to. */
        uint32_t binding;

        /**
         * @brief The resource to bind at `binding`.
         *
         * Exactly one of the three alternatives must be active:
         * - `BufferBinding` — a buffer with an offset and size.
         * - `shared_ptr<Texture>` — a texture resource.
         * - `shared_ptr<Sampler>` — a sampler state.
         */
        std::variant<BufferBinding, std::shared_ptr<Texture>, std::shared_ptr<Sampler>> resource;
    };

    /**
     * @brief Full description for creating a `BindGroup`.
     *
     * Specifies the layout the group conforms to and the concrete resources bound to
     * each slot declared in that layout.
     *
     * @code
     * BindGroupDescriptor desc{};
     * desc.layout  = layout;
     * desc.entries = {
     *     { 0, BufferBinding{ uniformBuffer, 0, sizeof(Uniforms) } },
     *     { 1, colorTexture },
     *     { 2, linearSampler },
     * };
     * auto bg = device->createBindGroup(desc);
     * @endcode
     */
    struct BindGroupDescriptor {
        /** @brief Resource entries, one per binding slot declared in `layout`. */
        std::vector<BindGroupEntryDescriptor> entries;

        /** @brief The layout this bind group must conform to. */
        std::shared_ptr<BindGroupLayout> layout;
    };

}
