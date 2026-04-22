#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief Dimensionality of a texture resource.
     *
     * Specified when creating a `Texture` or declaring a texture binding in a
     * `BindGroupLayoutDescriptor` entry (`EntryObjectStorage::texture::viewDimension`).
     *
     * Numeric values match Metal's `MTLTextureType` enum for zero-cost casting
     * on the Metal backend.
     */
    enum class TextureType {
        tt1d = 0,        ///< One-dimensional texture (width only).
        tt2d = 1,        ///< Two-dimensional texture (width × height). The most common type.
        tt3d = 2,        ///< Three-dimensional volume texture (width × height × depth).
        ttCube = 5,      ///< Cubemap view — 6 array layers interpreted as cube faces [+X, -X, +Y, -Y, +Z, -Z].
        ttCubeArray = 6, ///< Cubemap array view — multiple of 6 array layers, each group of 6 is one cubemap.
    };

}
