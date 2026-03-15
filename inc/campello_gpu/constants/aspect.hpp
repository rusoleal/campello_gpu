#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief Selects which aspect(s) of a depth/stencil texture a view exposes.
     *
     * Used when creating a `TextureView` from a depth/stencil texture to control
     * whether the view covers the depth component, the stencil component, or both.
     */
    enum class Aspect {
        all,         ///< Both depth and stencil aspects (or the full texture for colour formats).
        depthOnly,   ///< Only the depth component of a depth/stencil texture.
        stencilOnly  ///< Only the stencil component of a depth/stencil texture.
    };

}
