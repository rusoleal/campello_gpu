#pragma once

#include <memory>
#include <vector>
#include <optional>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/query_set.hpp>

namespace systems::leal::campello_gpu {

    enum class LoadOp {
        clear,
        load
    };

    enum class StoreOp {
        discard,
        store
    };

    struct ColorAttachment {
        float clearValue[4];
        uint32_t depthSlice;
        LoadOp loadOp;
        StoreOp storeOp;
        std::shared_ptr<TextureView> resolveTarget;
        std::shared_ptr<TextureView> view;

    };

    struct TimestampObject {
        std::shared_ptr<QuerySet> querySet;
        uint32_t beginningOfPassWriteIndex;
        uint32_t endOfPassWriteIndex;
    };

    struct DepthStencilAttachment{
        float depthClearValue;
        LoadOp depthLoadOp;
        bool depthReadOnly;
        StoreOp depthStoreOp;

        uint32_t stencilClearValue;
        LoadOp stencilLoadOp;
        bool stencilRadOnly;
        StoreOp stencilStoreOp;

        std::shared_ptr<TextureView> view;

    };

    struct BeginRenderPassDescriptor {

        std::vector<ColorAttachment> colorAttachments;

        std::optional<DepthStencilAttachment> depthStencilAttachment;

        //uint32_t maxDrawCount;

        std::shared_ptr<QuerySet> occlusionQuerySet;

        std::vector<TimestampObject> timestampWrites;

    };

}