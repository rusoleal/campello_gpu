#include <android/log.h>
#include <campello_gpu/command_encoder.hpp>
#include "command_encoder_handle.hpp"

using namespace systems::leal::campello_gpu;

CommandEncoder::CommandEncoder(void *pd) {
    this->native = pd;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","CommandEncoder::CommandEncoder()");
}

CommandEncoder::~CommandEncoder() {

    auto data = (CommandEncoderHandle *)this->native;

    delete data;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","CommandEncoder::~CommandEncoder()");
}

std::shared_ptr<RenderPassEncoder> CommandEncoder::beginRenderPass(const BeginRenderPassDescriptor &descriptor) {

}

void CommandEncoder::clearBuffer(std::shared_ptr<Buffer> buffer, uint64_t offset, uint64_t size) {

}

void CommandEncoder::copyBufferToBuffer(std::shared_ptr<Buffer> source, uint64_t sourceOffset, std::shared_ptr<Buffer> destination, uint64_t destinationOffset, uint64_t size) {

}

// TODO
void CommandEncoder::copyBufferToTexture() {

}

// TODO
void CommandEncoder::copyTextureToBuffer() {

}

// TODO
void CommandEncoder::copyTextureToTexture() {

}

std::shared_ptr<CommandBuffer> CommandEncoder::finish() {

}

void CommandEncoder::resolveQuerySet(std::shared_ptr<QuerySet> querySet, uint32_t firstQuery, uint32_t queryCount, std::shared_ptr<Buffer> destination, uint64_t destinationOffset) {

}

void CommandEncoder::writeTimestamp(std::shared_ptr<QuerySet> querySet, uint32_t queryIndex) {
    
}

