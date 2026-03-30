#pragma once

#include <cstdint>

namespace systems::leal::campello_gpu
{
    /**
     * @brief 3D signed offset for texture/buffer operations.
     * 
     * Used to specify the starting position within a texture for copy operations.
     * Components default to 0 (origin).
     */
    struct Offset3D
    {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;

        Offset3D() = default;
        Offset3D(int32_t x, int32_t y, int32_t z = 0) : x(x), y(y), z(z) {}

        static Offset3D make(int32_t x, int32_t y, int32_t z = 0)
        {
            return Offset3D(x, y, z);
        }
    };
} // namespace systems::leal::campello_gpu
