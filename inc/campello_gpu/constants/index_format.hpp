#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief Data type used for index buffer elements.
     *
     * Passed to `RenderPassEncoder::setIndexBuffer()` to tell the GPU how wide
     * each index value is within the bound index buffer.
     */
    enum class IndexFormat {
        uint16, ///< Each index is a 16-bit unsigned integer. Maximum index value: 65 535.
        uint32  ///< Each index is a 32-bit unsigned integer. Maximum index value: 4 294 967 295.
    };

}
