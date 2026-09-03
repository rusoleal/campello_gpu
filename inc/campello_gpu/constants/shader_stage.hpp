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

    /// Combines two stage flags — see `ShaderStage`'s doc comment. `enum
    /// class` has no implicit bitwise operators, so this (and `operator&`/
    /// `operator|=` below) is required for the documented usage to compile.
    constexpr ShaderStage operator|(ShaderStage lhs, ShaderStage rhs) noexcept
    {
        return static_cast<ShaderStage>(static_cast<unsigned>(lhs) | static_cast<unsigned>(rhs));
    }

    /// Tests whether `rhs`'s bit(s) are set in `lhs`.
    constexpr ShaderStage operator&(ShaderStage lhs, ShaderStage rhs) noexcept
    {
        return static_cast<ShaderStage>(static_cast<unsigned>(lhs) & static_cast<unsigned>(rhs));
    }

    constexpr ShaderStage& operator|=(ShaderStage& lhs, ShaderStage rhs) noexcept
    {
        lhs = lhs | rhs;
        return lhs;
    }

}
