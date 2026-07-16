#pragma once

#include <cstdint>
#include <campello_gpu/constants/cooperative_matrix_component_type.hpp>

namespace systems::leal::campello_gpu
{

    /**
     * @brief One hardware-supported cooperative-matrix tile shape/type combination.
     *
     * `Feature::cooperativeMatrix` only says *some* shape/type combination is
     * supported, not which one -- a shader compiled assuming a tile shape the
     * hardware doesn't actually support can fail validation or behave
     * incorrectly. Check `Device::getCooperativeMatrixProperties()` for a
     * matching entry before compiling/dispatching a cooperative-matrix
     * compute shader.
     *
     * Mirrors Vulkan's `VkCooperativeMatrixPropertiesKHR` (minus its `scope`
     * field, which is always subgroup-scope for this extension in practice).
     */
    struct CooperativeMatrixProperties
    {
        /** Rows of the A/result matrix and the C/result matrix. */
        uint32_t mSize = 0;
        /** Columns of the B/result matrix and the C/result matrix. */
        uint32_t nSize = 0;
        /** Columns of A / rows of B (the shared reduction dimension). */
        uint32_t kSize = 0;

        /** Element type of the A (left-hand) operand matrix. */
        CooperativeMatrixComponentType aType = CooperativeMatrixComponentType::float16;
        /** Element type of the B (right-hand) operand matrix. */
        CooperativeMatrixComponentType bType = CooperativeMatrixComponentType::float16;
        /** Element type of the C (accumulator input) matrix. */
        CooperativeMatrixComponentType cType = CooperativeMatrixComponentType::float16;
        /** Element type of the result matrix. */
        CooperativeMatrixComponentType resultType = CooperativeMatrixComponentType::float16;

        /** Whether accumulation saturates instead of wrapping/overflowing. */
        bool saturatingAccumulation = false;
    };

}
