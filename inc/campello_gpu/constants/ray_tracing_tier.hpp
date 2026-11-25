#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief How a device implements ray tracing.
     *
     * Reported in `RayTracingCapabilities::tier` by
     * `Adapter::getRayTracingCapabilities()` and
     * `Device::getRayTracingCapabilities()`.
     *
     * `Feature::raytracing` is present exactly when the tier is `hardware`, so
     * code that gates ray tracing effects on that feature never selects
     * campello_gpu's compute-shader emulation by accident.
     */
    enum class RayTracingTier {
        none,     ///< No ray tracing: every ray tracing entry point returns `nullptr` or does nothing.
        software, ///< Emulated in compute shaders: acceleration structures and inline ray
                  ///< queries work, at compute-shader speed (WebGPU).
        hardware  ///< The platform's native ray tracing API (DXR / Metal ray tracing /
                  ///< Vulkan KHR ray tracing).
    };

    // Note: `hardware` names who implements ray tracing (the platform's API), not
    // whether the GPU has ray tracing units. Metal reports it on every device where
    // `supportsRaytracing()` is true, including Intel/AMD Macs and Apple GPUs before
    // the M3/A17 generation, where Metal itself runs traversal in shader code. The
    // GPU's speed can't be known from the tier (and inside a browser not at all);
    // apps that need to know should time a calibration trace.

}
