#include <campello_gpu/validation_diagnostics.hpp>

// Fallback for backends without a Vulkan validation messenger (Metal,
// DirectX, WebGPU) -- see validation_diagnostics.hpp's doc comment. The
// Vulkan backend provides its own definitions in src/vulkan/device.cpp.

namespace systems::leal::campello_gpu {

    uint64_t validationErrorCount() { return 0; }
    void resetValidationErrorCount() {}

} // namespace systems::leal::campello_gpu
