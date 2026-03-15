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
        vertex   = 0x01, ///< The vertex shader stage.
        fragment = 0x02, ///< The fragment (pixel) shader stage.
        compute  = 0x04, ///< The compute shader stage.
    };

}
