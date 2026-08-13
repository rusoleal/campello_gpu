#pragma once

#include <cstdint>

namespace systems::leal::campello_gpu {

    /**
     * @brief Number of ERROR-severity Vulkan validation messages observed
     * since the last resetValidationErrorCount() call (or process start).
     *
     * Backed by the VK_EXT_debug_utils messenger installed when the library
     * is built with CAMPELLO_GPU_VALIDATION=ON (see device.cpp). Built
     * without it, this always returns 0 -- there is no messenger to count
     * from, so a test asserting on this value is only meaningful in a
     * validation-enabled build.
     *
     * Intended for tests: reset before an operation under test, then assert
     * this is still 0 afterward, instead of relying on a human reading
     * stderr for "[Vulkan ERROR]" lines.
     */
    uint64_t validationErrorCount();

    /** @brief Resets validationErrorCount()'s counter to 0. */
    void resetValidationErrorCount();

} // namespace systems::leal::campello_gpu
