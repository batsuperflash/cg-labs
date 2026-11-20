#pragma once
#include "graphics.hpp"

namespace shadows {

constexpr uint32_t resolution = 1024;

struct Resources {
    graphics::Texture sun;
    graphics::Texture spot;
    VkFormat format{};
    VkSampler sampler{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
};

bool create(Resources& resources, VkVertexInputBindingDescription binding,
            const VkVertexInputAttributeDescription& position);
void destroy(Resources& resources);
void begin(VkCommandBuffer cmd, const Resources& resources, const graphics::Texture& map, float bias);
void end(VkCommandBuffer cmd, const graphics::Texture& map);

}
