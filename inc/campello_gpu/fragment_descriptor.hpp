#pragma once

#include <memory>
#include <vector>
#include <campello_gpu/pixel_format.hpp>
#include <campello_gpu/shader_module.hpp>

namespace systems::leal::campello_gpu {

    /**
     * One or more bitwise flags defining the write mask to apply to the color target state.
     * Possible flag values are:
     */
    enum class ColorWrite {

        red=0x01,
        green = 0x02,
        blue = 0x04,
        alpha = 0x08,
        all = 0x0f
    };

    struct ColorState {

        /**
         * An enumerated value specifying the required format for output colors.
         */
        PixelFormat format;

        /**
         * One or more bitwise flags defining the write mask to apply to the color target state.
         * If omitted, writeMask defaults to ColorWrite.all.
         */
        ColorWrite writeMask;

    };

    struct FragmentDescriptor {

        std::shared_ptr<ShaderModule> module;

        std::string entryPoint;

        /**
         * an array of objects representing color states that represent configuration details for
         * the colors output by the fragment shader stage.
         */
        std::vector<ColorState> targets;

    };

}