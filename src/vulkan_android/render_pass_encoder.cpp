#include <android/log.h>
#include <campello_gpu/render_pass_encoder.hpp>

using namespace systems::leal::campello_gpu;

RenderPassEncoder::RenderPassEncoder(void *pd) {
    native = pd;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","RenderPassEncoder::RenderPassEncoder()");
}

RenderPassEncoder::~RenderPassEncoder() {

}

void RenderPassEncoder::beginOcclusionQuery(uint32_t queryIndex) {

}

void RenderPassEncoder::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {

}

void RenderPassEncoder::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t baseVertex, uint32_t firstInstance) {

}

void RenderPassEncoder::drawIndirect(std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset) {

}

void RenderPassEncoder::drawIndexedIndirect(std::shared_ptr<Buffer> indirectBuffer, uint64_t indirectOffset) {

}

void RenderPassEncoder::end() {

}

void RenderPassEncoder::endOcclusionQuery() {

}

//void RenderPassEncoder::executeBundles(bundles); // TODO
//void RenderPassEncoder::setBindGroup(uint32_t index, bindGroup, dynamicOffsets, dynamicOffsetsStart, dynamicOffsetsLength);
//void RenderPassEncoder::setBlendConstant(color);

void RenderPassEncoder::setIndexBuffer(std::shared_ptr<Buffer> buffer, IndexFormat indexFormat, uint64_t offset, int64_t size) {

}

void RenderPassEncoder::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {

}

void RenderPassEncoder::setScissorRect(float x, float y, float width, float height) {

}

void RenderPassEncoder::setStencilReference(uint32_t reference) {

}

void RenderPassEncoder::setVertexBuffer(uint32_t slot, std::shared_ptr<Buffer> buffer, uint64_t offset, int64_t size) {

}

void RenderPassEncoder::setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) {

}
