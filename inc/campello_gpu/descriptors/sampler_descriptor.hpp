#pragma once

#include <optional>
#include <campello_gpu/constants/wrap_mode.hpp>
#include <campello_gpu/constants/filter_mode.hpp>
#include <campello_gpu/constants/compare_op.hpp>

namespace systems::leal::campello_gpu {

    /**
     * @brief Configuration for creating a `Sampler`.
     *
     * Defines filtering modes, address (wrap) modes, LOD clamping, and optional
     * comparison function. Pass to `Device::createSampler()`.
     *
     * @code
     * SamplerDescriptor sd{};
     * sd.addressModeU = WrapMode::clampToEdge;
     * sd.addressModeV = WrapMode::clampToEdge;
     * sd.addressModeW = WrapMode::clampToEdge;
     * sd.magFilter    = FilterMode::fmLinear;
     * sd.minFilter    = FilterMode::fmLinear;
     * sd.lodMinClamp  = 0.0;
     * sd.lodMaxClamp  = 1000.0;
     * sd.maxAnisotropy = 1.0;
     * auto sampler = device->createSampler(sd);
     * @endcode
     */
    struct SamplerDescriptor {
        /**
         * @brief Wrap mode applied when the U texture coordinate exceeds [0, 1].
         */
        WrapMode addressModeU;

        /**
         * @brief Wrap mode applied when the V texture coordinate exceeds [0, 1].
         */
        WrapMode addressModeV;

        /**
         * @brief Wrap mode applied when the W texture coordinate exceeds [0, 1].
         *
         * Only relevant for 3D (`tt3d`) textures.
         */
        WrapMode addressModeW;

        /**
         * @brief Optional comparison function that makes this a comparison sampler.
         *
         * When set, the sampler performs depth/shadow comparisons rather than
         * regular filtering. Leave as `std::nullopt` for a standard filtering sampler.
         */
        std::optional<CompareOp> compare;

        /**
         * @brief Minimum level-of-detail (mip level) used during sampling.
         *
         * Clamped to `[0, lodMaxClamp]`. Defaults to `0.0`.
         */
        double lodMinClamp;

        /**
         * @brief Maximum level-of-detail (mip level) used during sampling.
         *
         * Clamped to `[lodMinClamp, ∞)`. Use a large value (e.g. `1000.0`) to
         * allow the full mip chain to be used.
         */
        double lodMaxClamp;

        /**
         * @brief Maximum anisotropy ratio for anisotropic filtering.
         *
         * Valid range is `[1, 16]`. Values above the platform maximum are clamped.
         * Set to `1.0` to disable anisotropic filtering.
         */
        double maxAnisotropy;

        /**
         * @brief Filter applied when the sampled area is smaller than one texel
         *        (magnification).
         */
        FilterMode magFilter;

        /**
         * @brief Filter applied when the sampled area is larger than one texel
         *        (minification).
         */
        FilterMode minFilter;
    };

}