#pragma once

#include <memory>
#include <vector>
#include <optional>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/query_set.hpp>

namespace systems::leal::campello_gpu {

    /** @brief Controls how the attachment is initialized at the start of the pass. */
    enum class LoadOp {
        clear,  ///< Clear the attachment to `clearValue` before the pass begins.
        load    ///< Preserve the existing contents of the attachment.
    };

    /** @brief Controls what happens to the attachment contents at the end of the pass. */
    enum class StoreOp {
        discard, ///< The attachment contents after the pass are undefined (may be discarded).
        store    ///< Write the results of the pass back to the attachment texture.
    };

    /**
     * @brief Configuration for a single color attachment in a render pass.
     *
     * Each color attachment maps to one output of the fragment shader. `view` is
     * required; `resolveTarget` is only needed for MSAA resolve passes.
     */
    struct ColorAttachment {
        /** @brief RGBA clear color used when `loadOp` is `LoadOp::clear`. */
        float clearValue[4];

        /** @brief Depth slice index for 3D texture attachments (use 0 for 2D). */
        uint32_t depthSlice;

        /** @brief Action performed on the attachment at the start of the pass. */
        LoadOp loadOp;

        /** @brief Action performed on the attachment at the end of the pass. */
        StoreOp storeOp;

        /** @brief Optional resolve target for MSAA — the single-sample texture to
         *         resolve into at the end of the pass. Leave `nullptr` for non-MSAA. */
        std::shared_ptr<TextureView> resolveTarget;

        /** @brief The texture view to render into. Must not be `nullptr`. */
        std::shared_ptr<TextureView> view;
    };

    /**
     * @brief Timestamp write request associated with a render or compute pass.
     *
     * Records GPU clock timestamps at the beginning and/or end of a pass into
     * the specified `QuerySet` slots.
     */
    struct TimestampObject {
        /** @brief The query set that receives the timestamp values. */
        std::shared_ptr<QuerySet> querySet;

        /** @brief Slot index written at the beginning of the pass. */
        uint32_t beginningOfPassWriteIndex;

        /** @brief Slot index written at the end of the pass. */
        uint32_t endOfPassWriteIndex;
    };

    /**
     * @brief Configuration for the depth and/or stencil attachment in a render pass.
     *
     * Set `view` to a depth/stencil `TextureView` created from a depth-format texture.
     * Leave `stencilLoadOp` / `stencilStoreOp` at their defaults if the format has no
     * stencil component.
     */
    struct DepthStencilAttachment {
        /** @brief Depth clear value used when `depthLoadOp` is `LoadOp::clear` (range: [0, 1]). */
        float depthClearValue;

        /** @brief Action performed on the depth component at the start of the pass. */
        LoadOp depthLoadOp;

        /** @brief When `true`, the pass does not write depth values (read-only depth). */
        bool depthReadOnly;

        /** @brief Action performed on the depth component at the end of the pass. */
        StoreOp depthStoreOp;

        /** @brief Stencil clear value used when `stencilLoadOp` is `LoadOp::clear`. */
        uint32_t stencilClearValue;

        /** @brief Action performed on the stencil component at the start of the pass. */
        LoadOp stencilLoadOp;

        /** @brief When `true`, the pass does not write stencil values (read-only stencil). */
        bool stencilRadOnly;

        /** @brief Action performed on the stencil component at the end of the pass. */
        StoreOp stencilStoreOp;

        /** @brief The depth/stencil texture view. Must not be `nullptr`. */
        std::shared_ptr<TextureView> view;
    };

    /**
     * @brief Full configuration for beginning a render pass.
     *
     * Pass this to `CommandEncoder::beginRenderPass()` to declare all attachments and
     * optional query objects for the pass.
     *
     * **Minimal color-only pass:**
     * @code
     * ColorAttachment ca{};
     * ca.view          = textureView;
     * ca.clearValue[0] = 0.0f; ca.clearValue[1] = 0.0f;
     * ca.clearValue[2] = 0.0f; ca.clearValue[3] = 1.0f;
     * ca.loadOp  = LoadOp::clear;
     * ca.storeOp = StoreOp::store;
     *
     * BeginRenderPassDescriptor desc{};
     * desc.colorAttachments = { ca };
     * auto rpe = encoder->beginRenderPass(desc);
     * @endcode
     */
    struct BeginRenderPassDescriptor {
        /** @brief One entry per fragment shader color output. At least one is required. */
        std::vector<ColorAttachment> colorAttachments;

        /** @brief Optional depth/stencil attachment. Omit for passes that have no depth test. */
        std::optional<DepthStencilAttachment> depthStencilAttachment;

        /** @brief Optional query set for occlusion queries within this pass. */
        std::shared_ptr<QuerySet> occlusionQuerySet;

        /** @brief Timestamp query writes to record at the start and end of this pass. */
        std::vector<TimestampObject> timestampWrites;
    };

}
