#pragma once

namespace systems::leal::campello_gpu
{

    /**
     * @brief Scalar element type of a cooperative-matrix operand or result.
     *
     * Mirrors Vulkan's `VkComponentTypeKHR`, the only backend that currently
     * exposes per-shape cooperative-matrix data — see
     * `Device::getCooperativeMatrixProperties()`.
     */
    enum class CooperativeMatrixComponentType
    {
        float16,
        float32,
        float64,
        sint8,
        sint16,
        sint32,
        sint64,
        uint8,
        uint16,
        uint32,
        uint64,
    };

}
