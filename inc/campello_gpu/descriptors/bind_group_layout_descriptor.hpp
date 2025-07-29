#pragma once

#include <vector>
#include <campello_gpu/constants/shader_stage.hpp>
#include <campello_gpu/constants/texture_type.hpp>

namespace systems::leal::campello_gpu {

    struct EntryObject;

    enum class EntryObjectType {
        buffer,
        texture,
        sampler
    };

    enum class EntryObjectBufferType {
        readOnlyStorage,
        storage,
        uniform
    };

    enum class EntryObjectSamplerType {
        comparison,
        filtering,
        nonFiltering
    };

    enum class EntryObjectTextureType {
        ttDepth,
        ttFloat,
        ttSint,
        ttUint,
        ttUnfilterableFloat
    };

    union EntryObjectStorage {
        struct {
            bool hasDinamicOffaset;
            uint32_t minBindingSize;
            EntryObjectBufferType type;
        } buffer;

        struct {
            EntryObjectSamplerType type;
        } sampler;

        struct {
            bool multisampled;
            EntryObjectTextureType sampleType;
            TextureType viewDimension;
        } texture;
    };

    struct BindGroupLayoutDescriptor {

        std::vector<EntryObject> entries;
    };

    struct EntryObject {

        uint32_t binding;

        ShaderStage visibility;

        EntryObjectType type;

        EntryObjectStorage data;

    };
}