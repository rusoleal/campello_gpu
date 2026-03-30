#pragma once

#include <cstdint>

namespace systems::leal::campello_gpu
{
    /**
     * @brief 3D unsigned extent (size) for texture/buffer operations.
     * 
     * Used to specify the dimensions of a region for copy operations.
     * Components default to 1 (minimum valid size).
     */
    struct Extent3D
    {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;

        Extent3D() = default;
        Extent3D(uint32_t width, uint32_t height, uint32_t depth = 1) 
            : width(width), height(height), depth(depth) {}

        static Extent3D make(uint32_t width, uint32_t height, uint32_t depth = 1)
        {
            return Extent3D(width, height, depth);
        }
    };
} // namespace systems::leal::campello_gpu
