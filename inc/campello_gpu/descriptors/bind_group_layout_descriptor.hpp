#pragma once

#include <vector>
#include <campello_gpu/constants/shader_stage.hpp>
#include <campello_gpu/constants/texture_type.hpp>

namespace systems::leal::campello_gpu {

    struct EntryObject;

    /** @brief The resource category of a bind group layout entry. */
    enum class EntryObjectType {
        buffer,  ///< A buffer resource (uniform, storage, or read-only storage).
        texture, ///< A texture resource.
        sampler  ///< A sampler state.
    };

    /** @brief The specific buffer binding type. */
    enum class EntryObjectBufferType {
        readOnlyStorage, ///< Read-only storage buffer (shader cannot write to it).
        storage,         ///< Read/write storage buffer.
        uniform          ///< Uniform (constant) buffer — small, frequently updated data.
    };

    /** @brief The specific sampler binding type. */
    enum class EntryObjectSamplerType {
        comparison,   ///< Depth/shadow comparison sampler.
        filtering,    ///< Regular filtering sampler (linear, nearest, anisotropic).
        nonFiltering  ///< Non-filtering sampler (nearest only, no mip interpolation).
    };

    /** @brief The texture sample type expected by the shader. */
    enum class EntryObjectTextureType {
        ttDepth,             ///< Depth texture.
        ttFloat,             ///< Floating-point texture (filterable).
        ttSint,              ///< Signed integer texture.
        ttUint,              ///< Unsigned integer texture.
        ttUnfilterableFloat  ///< Floating-point texture that cannot be filtered.
    };

    /**
     * @brief Per-entry storage describing the specific resource type and its properties.
     *
     * Only the member matching `EntryObject::type` is active.
     */
    union EntryObjectStorage {
        /** @brief Properties for a buffer binding (`EntryObjectType::buffer`). */
        struct {
            bool hasDinamicOffaset;      ///< Whether this binding uses a dynamic offset.
            uint32_t minBindingSize;     ///< Minimum byte size of the bound buffer region.
            EntryObjectBufferType type;  ///< Buffer sub-type (uniform / storage / read-only storage).
        } buffer;

        /** @brief Properties for a sampler binding (`EntryObjectType::sampler`). */
        struct {
            EntryObjectSamplerType type; ///< Sampler sub-type.
        } sampler;

        /** @brief Properties for a texture binding (`EntryObjectType::texture`). */
        struct {
            bool multisampled;                ///< Whether the texture is multisampled.
            EntryObjectTextureType sampleType; ///< Expected sample type.
            TextureType viewDimension;         ///< Expected texture dimensionality.
        } texture;
    };

    /**
     * @brief Full description for creating a `BindGroupLayout`.
     *
     * Contains one `EntryObject` for each resource binding slot that pipelines using
     * this layout will expose to shaders.
     */
    struct BindGroupLayoutDescriptor {
        /** @brief Ordered list of binding entries; the index in the vector is not the
         *         binding number — use `EntryObject::binding` for the slot index. */
        std::vector<EntryObject> entries;
    };

    /**
     * @brief Describes a single resource binding slot in a `BindGroupLayout`.
     *
     * Defines the binding index, the shader stages that can access it, the resource
     * category, and category-specific properties.
     */
    struct EntryObject {
        /** @brief Binding index referenced in shader source (e.g. `[[buffer(0)]]`). */
        uint32_t binding;

        /** @brief Bitmask of `ShaderStage` flags indicating which stages can access this binding. */
        ShaderStage visibility;

        /** @brief Resource category: buffer, texture, or sampler. */
        EntryObjectType type;

        /** @brief Category-specific properties — read the member matching `type`. */
        EntryObjectStorage data;
    };
}
