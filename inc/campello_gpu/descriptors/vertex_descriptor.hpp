#pragma once

#include <memory>
#include <string>
#include <vector>
#include <campello_gpu/shader_module.hpp>

namespace systems::leal::campello_gpu {

    /**
     * @brief Data type of each component within a vertex attribute accessor.
     *
     * Numeric values match the glTF `componentType` constants for interoperability.
     */
    enum class ComponentType
    {
        ctByte          = 5120, ///< Signed 8-bit integer.
        ctUnsignedByte  = 5121, ///< Unsigned 8-bit integer.
        ctShort         = 5122, ///< Signed 16-bit integer.
        ctUnsignedShort = 5123, ///< Unsigned 16-bit integer.
        ctUnsignedInt   = 5125, ///< Unsigned 32-bit integer.
        ctFloat         = 5126, ///< 32-bit IEEE 754 float.
    };

    /**
     * @brief Shape of the elements produced by a vertex attribute accessor.
     *
     * Numeric values match glTF `accessor.type` identifiers.
     */
    enum class AccessorType
    {
        acScalar, ///< Single scalar value per element.
        acVec2,   ///< 2-component vector per element.
        acVec3,   ///< 3-component vector per element.
        acVec4,   ///< 4-component vector per element.
        acMat2,   ///< 2×2 column-major matrix per element.
        acMat3,   ///< 3×3 column-major matrix per element.
        acMat4    ///< 4×4 column-major matrix per element.
    };

    /**
     * @brief Controls how the GPU advances through a vertex buffer.
     *
     * - `vertex`   — advance one element per vertex draw (default instanced data rate).
     * - `instance` — advance one element per instance.
     */
    enum class StepMode {
        instance, ///< Advance once per rendered instance.
        vertex    ///< Advance once per rendered vertex.
    };

    /**
     * @brief Description of a single vertex attribute fetched from a buffer.
     *
     * Maps a buffer region (identified by a `VertexLayout`) to a shader input
     * location, specifying the data type and byte offset within each element.
     */
    struct VertexAttribute {
        /** @brief Scalar component type (e.g. float, unsigned byte). */
        ComponentType componentType;

        /** @brief Number of components / shape (scalar, vec2, vec3 …). */
        AccessorType accessorType;

        /** @brief Byte offset of this attribute within one buffer element. */
        uint32_t offset;

        /** @brief Shader input location index this attribute feeds into. */
        uint32_t shaderLocation;
    };

    /**
     * @brief Describes the layout of one vertex buffer binding.
     *
     * A single `VertexLayout` can carry multiple interleaved attributes. The
     * GPU reads `arrayStride` bytes per step (vertex or instance) from the
     * bound buffer.
     */
    struct VertexLayout {
        /** @brief Byte stride between consecutive elements in the buffer. */
        double arrayStride;

        /** @brief Attributes packed into each element of this buffer. */
        std::vector<VertexAttribute> attributes;

        /** @brief Whether the buffer advances per vertex or per instance. */
        StepMode stepMode;
    };

    /**
     * @brief Configuration for the vertex shader stage of a render pipeline.
     *
     * Specifies the shader entry point and the buffer bindings that provide
     * per-vertex or per-instance data to that entry point.
     *
     * @code
     * VertexDescriptor vd{};
     * vd.module     = vertShader;
     * vd.entryPoint = "vertexMain";
     * // No buffers needed for hardcoded geometry (e.g. vertex_id tricks)
     * @endcode
     */
    struct VertexDescriptor {
        /**
         * @brief Shader module containing the vertex entry point.
         */
        std::shared_ptr<ShaderModule> module;

        /**
         * @brief Name of the vertex function within `module`.
         */
        std::string entryPoint;

        /**
         * @brief Vertex buffer layout descriptors, one per bound buffer slot.
         *
         * An empty vector is valid for pipelines that generate geometry
         * procedurally using the vertex ID (e.g. fullscreen-triangle trick).
         */
        std::vector<VertexLayout> buffers;
    };

}
