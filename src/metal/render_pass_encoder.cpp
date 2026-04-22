#include "Metal.hpp"
#include "render_pipeline_handle.hpp"
#include "bind_group_data.hpp"
#include "buffer_handle.hpp"
#include "texture_handle.hpp"
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/sampler.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/constants/shader_stage.hpp>
#include <unordered_map>
#include <utility>

using namespace systems::leal::campello_gpu;

// Internal state needed to support setIndexBuffer + drawIndexed.
struct MetalRenderEncoderData {
    MTL::RenderCommandEncoder *encoder;
    MTL::Buffer               *indexBuffer;
    MTL::IndexType             indexType;
    NS::UInteger               indexOffset;

    // Pipeline state cache — avoids redundant setRenderPipelineState calls.
    MTL::RenderPipelineState  *currentPipelineState = nullptr;
    MTL::DepthStencilState    *currentDepthStencilState = nullptr;

    // Resource binding cache — avoids redundant encoder calls that trigger
    // Metal API Validation asserts.
    std::unordered_map<uint32_t, MTL::SamplerState*> vertexSamplers;
    std::unordered_map<uint32_t, MTL::SamplerState*> fragmentSamplers;
    std::unordered_map<uint32_t, MTL::Texture*>       vertexTextures;
    std::unordered_map<uint32_t, MTL::Texture*>       fragmentTextures;
    std::unordered_map<uint32_t, std::pair<MTL::Buffer*, NS::UInteger>> vertexBuffers;
    std::unordered_map<uint32_t, std::pair<MTL::Buffer*, NS::UInteger>> fragmentBuffers;

    MetalRenderEncoderData(MTL::RenderCommandEncoder *enc)
        : encoder(enc), indexBuffer(nullptr),
          indexType(MTL::IndexTypeUInt16), indexOffset(0) {}
};

RenderPassEncoder::RenderPassEncoder(void *pd) {
    auto *enc = static_cast<MTL::RenderCommandEncoder *>(pd);
    enc->retain(); // renderCommandEncoder() is autoreleased; retain to take ownership
    native = new MetalRenderEncoderData(enc);
}

RenderPassEncoder::~RenderPassEncoder() {
    if (native != nullptr) {
        auto *data = static_cast<MetalRenderEncoderData *>(native);
        data->encoder->release();
        delete data;
    }
}

void RenderPassEncoder::beginOcclusionQuery(uint32_t queryIndex) {
    auto *enc = static_cast<MetalRenderEncoderData *>(native)->encoder;
    enc->setVisibilityResultMode(
        MTL::VisibilityResultModeCounting, queryIndex * sizeof(uint64_t));
}

void RenderPassEncoder::draw(uint32_t vertexCount, uint32_t instanceCount,
                             uint32_t firstVertex, uint32_t firstInstance) {
    auto *enc = static_cast<MetalRenderEncoderData *>(native)->encoder;
    enc->drawPrimitives(
        MTL::PrimitiveTypeTriangle,
        firstVertex, vertexCount, instanceCount, firstInstance);
}

void RenderPassEncoder::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                    uint32_t firstVertex, uint32_t baseVertex,
                                    uint32_t firstInstance) {
    auto *data = static_cast<MetalRenderEncoderData *>(native);
    if (!data->indexBuffer) return;
    data->encoder->drawIndexedPrimitives(
        MTL::PrimitiveTypeTriangle,
        indexCount,
        data->indexType,
        data->indexBuffer,
        data->indexOffset,
        instanceCount,
        (NS::Integer)baseVertex,
        firstInstance);
}

void RenderPassEncoder::drawIndirect(std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset) {
    auto *enc = static_cast<MetalRenderEncoderData *>(native)->encoder;
    auto *bufHandle = static_cast<MetalBufferHandle *>(indirectBuffer->native);
    enc->drawPrimitives(
        MTL::PrimitiveTypeTriangle,
        bufHandle->buffer,
        indirectOffset);
}

void RenderPassEncoder::drawIndexedIndirect(std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset) {
    auto *data = static_cast<MetalRenderEncoderData *>(native);
    if (!data->indexBuffer) return;
    auto *bufHandle = static_cast<MetalBufferHandle *>(indirectBuffer->native);
    data->encoder->drawIndexedPrimitives(
        MTL::PrimitiveTypeTriangle,
        data->indexType,
        data->indexBuffer,
        data->indexOffset,
        bufHandle->buffer,
        indirectOffset);
}

void RenderPassEncoder::end() {
    static_cast<MetalRenderEncoderData *>(native)->encoder->endEncoding();
}

void RenderPassEncoder::endOcclusionQuery() {
    auto *enc = static_cast<MetalRenderEncoderData *>(native)->encoder;
    enc->setVisibilityResultMode(MTL::VisibilityResultModeDisabled, 0);
}

void RenderPassEncoder::setIndexBuffer(std::shared_ptr<Buffer> buffer,
                                       IndexFormat indexFormat,
                                       uint64_t offset, int64_t size) {
    auto *data        = static_cast<MetalRenderEncoderData *>(native);
    auto *bufHandle   = static_cast<MetalBufferHandle *>(buffer->native);
    data->indexBuffer = bufHandle->buffer;
    data->indexType   = (indexFormat == IndexFormat::uint32)
                            ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16;
    data->indexOffset = (NS::UInteger)offset;
}

void RenderPassEncoder::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
    auto *data   = static_cast<MetalRenderEncoderData *>(native);
    auto *enc    = data->encoder;
    auto *handle = static_cast<MetalRenderPipelineData *>(pipeline->native);
    if (data->currentPipelineState != handle->pipelineState) {
        enc->setRenderPipelineState(handle->pipelineState);
        data->currentPipelineState = handle->pipelineState;
    }
    if (handle->depthStencilState && data->currentDepthStencilState != handle->depthStencilState) {
        enc->setDepthStencilState(handle->depthStencilState);
        data->currentDepthStencilState = handle->depthStencilState;
    }
}

void RenderPassEncoder::setScissorRect(float x, float y, float width, float height) {
    MTL::ScissorRect rect;
    rect.x      = (NS::UInteger)x;
    rect.y      = (NS::UInteger)y;
    rect.width  = (NS::UInteger)width;
    rect.height = (NS::UInteger)height;
    static_cast<MetalRenderEncoderData *>(native)->encoder->setScissorRect(rect);
}

void RenderPassEncoder::setStencilReference(uint32_t reference) {
    static_cast<MetalRenderEncoderData *>(native)->encoder->setStencilReferenceValue(reference);
}

void RenderPassEncoder::setVertexBuffer(uint32_t slot, std::shared_ptr<Buffer> buffer,
                                        uint64_t offset, int64_t size) {
    auto *enc = static_cast<MetalRenderEncoderData *>(native)->encoder;
    auto *bufHandle = static_cast<MetalBufferHandle *>(buffer->native);
    enc->setVertexBuffer(bufHandle->buffer, offset, slot);
}

void RenderPassEncoder::setViewport(float x, float y, float width, float height,
                                    float minDepth, float maxDepth) {
    MTL::Viewport vp;
    vp.originX = x;
    vp.originY = y;
    vp.width   = width;
    vp.height  = height;
    vp.znear   = minDepth;
    vp.zfar    = maxDepth;
    static_cast<MetalRenderEncoderData *>(native)->encoder->setViewport(vp);
}

void RenderPassEncoder::setBindGroup(uint32_t index, std::shared_ptr<BindGroup> bindGroup,
                                     const std::vector<uint32_t> &dynamicOffsets,
                                     uint64_t dynamicOffsetsStart,
                                     uint64_t dynamicOffsetsLength) {
    (void)index; (void)dynamicOffsets; (void)dynamicOffsetsStart; (void)dynamicOffsetsLength;
    if (!native) return;
    if (!bindGroup) return;
    auto *data   = static_cast<MetalRenderEncoderData *>(native);
    auto *enc    = data->encoder;
    if (!enc) return;
    auto *bgData = static_cast<MetalBindGroupData *>(bindGroup->native);
    if (!bgData) return;

    for (const auto &entry : bgData->entries) {
        auto visIt = bgData->visibility.find(entry.binding);
        ShaderStage visibility = (visIt != bgData->visibility.end())
            ? visIt->second
            : static_cast<ShaderStage>(
                  static_cast<uint32_t>(ShaderStage::vertex) |
                  static_cast<uint32_t>(ShaderStage::fragment));

        bool vertexVisible   = static_cast<uint32_t>(visibility) & static_cast<uint32_t>(ShaderStage::vertex);
        bool fragmentVisible = static_cast<uint32_t>(visibility) & static_cast<uint32_t>(ShaderStage::fragment);

        if (std::holds_alternative<std::shared_ptr<Texture>>(entry.resource)) {
            const auto &texPtr = std::get<std::shared_ptr<Texture>>(entry.resource);
            if (!texPtr || !texPtr->native) continue;
            auto *texHandle = static_cast<MetalTextureHandle *>(texPtr->native);
            if (!texHandle->texture) continue;
            if (vertexVisible) {
                auto it = data->vertexTextures.find(entry.binding);
                if (it == data->vertexTextures.end() || it->second != texHandle->texture) {
                    enc->setVertexTexture(texHandle->texture, entry.binding);
                    data->vertexTextures[entry.binding] = texHandle->texture;
                }
            }
            if (fragmentVisible) {
                auto it = data->fragmentTextures.find(entry.binding);
                if (it == data->fragmentTextures.end() || it->second != texHandle->texture) {
                    enc->setFragmentTexture(texHandle->texture, entry.binding);
                    data->fragmentTextures[entry.binding] = texHandle->texture;
                }
            }
        } else if (std::holds_alternative<std::shared_ptr<Sampler>>(entry.resource)) {
            const auto &sampPtr = std::get<std::shared_ptr<Sampler>>(entry.resource);
            if (!sampPtr || !sampPtr->native) continue;
            auto *samp = static_cast<MTL::SamplerState *>(sampPtr->native);
            if (vertexVisible) {
                auto it = data->vertexSamplers.find(entry.binding);
                if (it == data->vertexSamplers.end() || it->second != samp) {
                    enc->setVertexSamplerState(samp, entry.binding);
                    data->vertexSamplers[entry.binding] = samp;
                }
            }
            if (fragmentVisible) {
                auto it = data->fragmentSamplers.find(entry.binding);
                if (it == data->fragmentSamplers.end() || it->second != samp) {
                    enc->setFragmentSamplerState(samp, entry.binding);
                    data->fragmentSamplers[entry.binding] = samp;
                }
            }
        } else if (std::holds_alternative<BufferBinding>(entry.resource)) {
            const auto &bb  = std::get<BufferBinding>(entry.resource);
            if (!bb.buffer || !bb.buffer->native) continue;
            auto *bufHandle = static_cast<MetalBufferHandle *>(bb.buffer->native);
            if (!bufHandle->buffer) continue;
            auto key = std::make_pair(bufHandle->buffer, static_cast<NS::UInteger>(bb.offset));
            if (vertexVisible) {
                auto it = data->vertexBuffers.find(entry.binding);
                if (it == data->vertexBuffers.end() || it->second != key) {
                    enc->setVertexBuffer(bufHandle->buffer, bb.offset, entry.binding);
                    data->vertexBuffers[entry.binding] = key;
                }
            }
            if (fragmentVisible) {
                auto it = data->fragmentBuffers.find(entry.binding);
                if (it == data->fragmentBuffers.end() || it->second != key) {
                    enc->setFragmentBuffer(bufHandle->buffer, bb.offset, entry.binding);
                    data->fragmentBuffers[entry.binding] = key;
                }
            }
        }
    }
}
