#include <cassert>
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

VkAttachmentLoadOp getLoadOp(LoadOp loadOp) {
    switch (loadOp) {
        case LoadOp::load:
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadOp::clear:
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        default:
            assert(false);
    }
}

VkAttachmentStoreOp getStoreOp(StoreOp storeOp) {
    switch (storeOp) {
        case StoreOp::store:
            return VK_ATTACHMENT_STORE_OP_STORE;
        case StoreOp::discard:
            return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        default:
            assert(false);
    }
}

std::shared_ptr<RenderPassEncoder> CommandEncoder::beginRenderPass(const BeginRenderPassDescriptor &descriptor) {

    auto data = (CommandEncoderHandle *)this->native;

    VkCommandBufferBeginInfo info;
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.pNext = nullptr;
    info.flags = 0;
    info.pInheritanceInfo = nullptr;

    if (vkBeginCommandBuffer(data->commandBuffer, &info) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","CommandEncoder::beginRenderPass() failed");
        return nullptr;
    }

    /*VkRenderPassBeginInfo rpinfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = descriptor.renderPass->native,
            .framebuffer = descriptor.framebuffer->native,
    };
    vkCmdBeginRenderPass(data->commandBuffer, &rpinfo, VK_SUBPASS_CONTENTS_INLINE);
*/
    std::vector<VkRenderingAttachmentInfo> colorAttachment(descriptor.colorAttachments.size());
    for (uint32_t a=0; a<descriptor.colorAttachments.size(); a++) {
        colorAttachment[a].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment[a].pNext = nullptr;
        colorAttachment[a].imageView = data->swapchainImageViews[0];
        colorAttachment[a].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment[a].resolveMode = VK_RESOLVE_MODE_NONE;
        colorAttachment[a].resolveImageView = VK_NULL_HANDLE;
        colorAttachment[a].resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment[a].loadOp = getLoadOp(descriptor.colorAttachments[a].loadOp);
        colorAttachment[a].storeOp = getStoreOp(descriptor.colorAttachments[a].storeOp);
        colorAttachment[a].clearValue = {
                .color = {
                        .float32 = {
                                descriptor.colorAttachments[a].clearValue[0],
                                descriptor.colorAttachments[a].clearValue[1],
                                descriptor.colorAttachments[a].clearValue[2],
                                descriptor.colorAttachments[a].clearValue[3],
                        }
                }
        };
    }
    VkRenderingAttachmentInfo depthAttachment = {};
    VkRenderingAttachmentInfo stencilAttachment = {};

    VkRenderingInfo rinfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .pNext = nullptr,
            .flags = 0,
            .renderArea.offset = { .x=0, .y=0 },
            .renderArea.extent = data->imageExtent,
            .layerCount = 1,
            .viewMask = 0,
            .colorAttachmentCount = (uint32_t)colorAttachment.size(),
            .pColorAttachments = colorAttachment.data(),
            .pDepthAttachment = descriptor.depthStencilAttachment.has_value()?&depthAttachment:VK_NULL_HANDLE,
            .pStencilAttachment = descriptor.depthStencilAttachment.has_value()?&stencilAttachment:VK_NULL_HANDLE,
    };
    vkCmdBeginRendering(data->commandBuffer, &rinfo);

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

