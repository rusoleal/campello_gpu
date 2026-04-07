#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief Bitmask flags identifying one or more shader pipeline stages.
     *
     * Used in `EntryObject::visibility` to declare which stages can access a
     * given resource binding.  Combine flags with bitwise OR.
     *
     * @code
     * // A binding visible to both vertex and fragment stages:
     * entry.visibility = ShaderStage::vertex | ShaderStage::fragment;
     * @endcode
     */
    enum class ShaderStage {
        vertex       = 0x01, ///< The vertex shader stage.
        fragment     = 0x02, ///< The fragment (pixel) shader stage.
        compute      = 0x04, ///< The compute shader stage.
        rayGeneration = 0x08, ///< Ray generation shader stage (ray tracing).
        miss          = 0x10, ///< Miss shader stage — invoked when a ray finds no intersection (ray tracing).
        closestHit    = 0x20, ///< Closest-hit shader stage — invoked for the nearest intersection (ray tracing).
        anyHit        = 0x40, ///< Any-hit shader stage — invoked for each candidate intersection (ray tracing).
        intersection  = 0x80, ///< Intersection shader stage — custom primitive intersection test (ray tracing).
    };

}
