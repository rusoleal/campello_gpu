#pragma once

#include <cstdint>

namespace systems::leal::campello_gpu {

    /**
     * @brief Type of a pipeline-time shader constant (ShaderConstant).
     *
     * It must match the constant's declaration in the shader: a Metal
     * `[[function_constant(id)]]` of type bool / int / uint / float, a SPIR-V
     * specialization constant (`layout(constant_id = id) const ...`), or a
     * WGSL `@id(id) override`.
     */
    enum class ShaderConstantType : uint8_t {
        boolean,
        int32,
        uint32,
        float32,
    };

    /**
     * @brief A value fixed when a pipeline is created (specialization).
     *
     * The shader compiler folds the value into the pipeline's code, so branches
     * on it cost nothing at run time: one shader source, a specialized
     * pipeline per value set.
     *
     * - Metal: function constants (`MTLFunctionConstantValues`), `id` = the
     *   `[[function_constant(id)]]` index.
     * - Vulkan: specialization constants (`VkSpecializationInfo`), `id` = the
     *   `constant_id`.
     * - WebGPU: pipeline-overridable constants, `id` = the WGSL `@id(id)`.
     * - DirectX: not supported; the shader's defaults apply.
     *
     * A constant the shader doesn't declare is ignored.
     */
    struct ShaderConstant {
        uint32_t id = 0;
        ShaderConstantType type = ShaderConstantType::uint32;
        double value = 0.0;
    };

}
