#pragma once

#include <optional>
#include <campello_gpu/constants/wrap_mode.hpp>
#include <campello_gpu/constants/filter_mode.hpp>
#include <campello_gpu/constants/compare_op.hpp>

namespace systems::leal::campello_gpu {

    struct SamplerDescriptor {

        /**
         * An enumerated value specifying the behavior of the sampler when the sample footprint width 
         * extends beyond the width of the texture.
         */
        WrapMode addressModeU;

        /**
         * An enumerated value specifying the behavior of the sampler when the sample footprint height
         * extends beyond the height of the texture.
         */
        WrapMode addressModeV;

        /**
         * An enumerated value specifying the behavior of the sampler when the sample footprint depth
         * extends beyond the depth of the texture.
         */
        WrapMode addressModeW;

        /**
         * If specified, the sampler will be a comparison sampler of the specified type.
         */
        std::optional<CompareOp> compare;

        /**
         * A number specifying the minimum level of detail used internally when sampling a texture.
         */
        double lodMinClamp;

        /**
         * A number specifying the maximum level of detail used internally when sampling a texture.
         */
        double lodMaxClamp;

        /**
         * Specifies the maximum anisotropy value clamp used by the sampler. 
         * 
         * Most implementations support maxAnisotropy values in a range between 1 and 16, inclusive.
         * The value used will be clamped to the maximum value that the underlying platform supports.
         */
        double maxAnisotropy;

        /**
         * An enumerated value specifying the sampling behavior when the sample footprint is smaller
         * than or equal to one texel.
         */
        FilterMode magFilter;

        /**
         * An enumerated value specifying the sampling behavior when the sample footprint is larger
         * than one texel.
         */
        FilterMode minFilter;

        
    };

}