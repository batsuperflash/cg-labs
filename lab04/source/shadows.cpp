#include "shadows.hpp"
#include "graphics_internal.hpp"
#include <iostream>

namespace shadows {
using graphics::internal::context;

namespace {
constexpr VkImageSubresourceRange depth_range{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
constexpr VkPipelineStageFlags depth_stages =
    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

bool createMap(VkFormat format, graphics::Texture& map) {
    const VkImageCreateInfo image{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = format, .extent = {resolution, resolution, 1},
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    };
    const VmaAllocationCreateInfo allocation{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (vmaCreateImage(context.allocator, &image, &allocation, &map.image, &map.allocation, nullptr) != VK_SUCCESS)
        return false;
    const VkImageViewCreateInfo view{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = map.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format, .subresourceRange = depth_range,
    };
    return vkCreateImageView(context.device, &view, nullptr, &map.view) == VK_SUCCESS;
}

void barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
             VkPipelineStageFlags src, VkPipelineStageFlags dst, VkAccessFlags reads, VkAccessFlags writes) {
    const VkImageMemoryBarrier change{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = reads, .dstAccessMask = writes,
        .oldLayout = from, .newLayout = to, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image, .subresourceRange = depth_range,
    };
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &change);
}
}

bool create(Resources& r, VkVertexInputBindingDescription binding, const VkVertexInputAttributeDescription& position) {
    for (auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(context.physical_device, format, &properties);
        constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & required) == required) {
            r.format = format;
            break;
        }
    }
    if (r.format == VK_FORMAT_UNDEFINED || !createMap(r.format, r.sun) || !createMap(r.format, r.spot))
        return false;
    const VkSamplerCreateInfo sampler{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .compareEnable = VK_TRUE, .compareOp = VK_COMPARE_OP_LESS,
    };
    if (vkCreateSampler(context.device, &sampler, nullptr, &r.sampler) != VK_SUCCESS) return false;
    const VkPushConstantRange transform{VK_SHADER_STAGE_VERTEX_BIT, 0, 16 * sizeof(float)};
    const VkPipelineLayoutCreateInfo layout{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &transform,
    };
    if (vkCreatePipelineLayout(context.device, &layout, nullptr, &r.layout) != VK_SUCCESS) return false;
    const auto shader = graphics::loadShaderModule("shaders/shadow.vert.spv");
    if (!shader) return false;
    r.pipeline = graphics::createPipeline({
        .vertex_shader = shader, .fragment_shader = VK_NULL_HANDLE, .layout = r.layout,
        .vertex_binding = binding, .vertex_attributes = {&position, 1}, .shadow_format = r.format,
    });
    vkDestroyShaderModule(context.device, shader, nullptr);
    return r.pipeline != VK_NULL_HANDLE;
}

void destroy(Resources& r) {
    vkDestroyPipeline(context.device, r.pipeline, nullptr);
    vkDestroyPipelineLayout(context.device, r.layout, nullptr);
    vkDestroySampler(context.device, r.sampler, nullptr);
    graphics::destroyTexture(r.sun);
    graphics::destroyTexture(r.spot);
}

void begin(VkCommandBuffer cmd, const Resources& r, const graphics::Texture& map, float bias) {
    // Each pass clears the entire map; the previous frame has completed at the frame fence.
    barrier(cmd, map.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, depth_stages, 0,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    const VkRenderingAttachmentInfo depth{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = map.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.depthStencil = {1.0f, 0}},
    };
    const VkRect2D area{.extent = {resolution, resolution}};
    const VkRenderingInfo rendering{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = area, .layerCount = 1,
        .pDepthAttachment = &depth,
    };
    vkCmdBeginRendering(cmd, &rendering);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.pipeline);
    const VkViewport viewport{0, 0, float(resolution), float(resolution), 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &area);
    vkCmdSetDepthBias(cmd, bias, 0, bias);
}

void end(VkCommandBuffer cmd, const graphics::Texture& map) {
    vkCmdEndRendering(cmd);
    barrier(cmd, map.image, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            depth_stages, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT);
}
}
