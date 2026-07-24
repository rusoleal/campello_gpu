#pragma once

#include <campello_gpu/constants/shader_stage.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace systems::leal::campello_gpu {

    class BindGroupLayout;

    /**
     * @brief A small, inline range of per-draw data pushed directly into
     * command-buffer state rather than read from a bound buffer.
     *
     * Backed by Vulkan push constants (guaranteed at least 128 bytes across
     * all ranges combined) where supported; ignored on backends that don't
     * need this concept (e.g. Metal binds small per-draw data through a
     * plain vertex buffer slot instead — see `RenderPassEncoder::
     * setPushConstants()`'s doc comment for the full rationale). Intended
     * for small, genuinely-per-draw-varying data (a handful of floats) that
     * would otherwise force a fresh descriptor-set allocation every draw
     * call just to update a few bytes.
     */
    struct PushConstantRange {
        ShaderStage stages;       ///< Which shader stage(s) read this range.
        uint32_t    offset = 0;   ///< Byte offset within the combined push-constant block.
        uint32_t    size   = 0;   ///< Byte size of this range.
    };

    /**
     * @brief Configuration for creating a `PipelineLayout`.
     *
     * A `PipelineLayout` declares the bind group layouts used by a pipeline,
     * plus any push-constant ranges (see `PushConstantRange`'s doc comment).
     *
     * @code
     * PipelineLayoutDescriptor desc{};
     * desc.bindGroupLayouts = { layout0, layout1 };
     * auto layout = device->createPipelineLayout(desc);
     * @endcode
     */
    struct PipelineLayoutDescriptor {
        std::vector<std::shared_ptr<BindGroupLayout>> bindGroupLayouts;
        std::vector<PushConstantRange>                pushConstantRanges;
    };

}
