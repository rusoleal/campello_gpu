#pragma once

#include <variant>
#include <vector>
#include <memory>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/sampler.hpp>

namespace systems::leal::campello_gpu {

    struct BufferBinding {
        std::shared_ptr<Buffer> buffer;
        uint64_t offset;
        uint64_t size;
    };

    struct BindGroupEntryDescriptor {
        uint32_t binding;
        std::variant<BufferBinding, std::shared_ptr<Texture>, std::shared_ptr<Sampler>> resource;
    };

    struct BindGroupDescriptor {
        std::vector<BindGroupEntryDescriptor> entries;
        std::shared_ptr<BindGroupLayout> layout;
    };

}
