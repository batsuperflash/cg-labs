#include "graphics.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include "graphics_internal.hpp"
#include "image.hpp"

namespace graphics {

using internal::context;

namespace {

VkCommandPool transfer_pool = VK_NULL_HANDLE;

// Записывает команды в одноразовый буфер команд, отправляет на очередь и ждёт завершения.
template <typename Record>
bool submitAndWait(Record&& record) {
	const VkCommandBufferAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = transfer_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	if (vkAllocateCommandBuffers(context.device, &allocate_info, &command_buffer) != VK_SUCCESS) {
		std::cerr << "Failed to allocate Vulkan command buffer for a copy\n";
		return false;
	}

	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(command_buffer, &begin_info);
	record(command_buffer);
	vkEndCommandBuffer(command_buffer);

	const VkSubmitInfo submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &command_buffer,
	};
	const bool ok = vkQueueSubmit(context.graphics_queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
	if (ok) {
		vkQueueWaitIdle(context.graphics_queue);
	} else {
		std::cerr << "Failed to submit Vulkan copy commands\n";
	}

	vkFreeCommandBuffers(context.device, transfer_pool, 1, &command_buffer);
	return ok;
}

// Барьер для одного диапазона мип-уровней: меняет layout и делает записи предыдущей операции видимыми следующей.
void transitionImage(VkCommandBuffer command_buffer, VkImage image, uint32_t base_mip, uint32_t mip_count,
                     VkImageLayout old_layout, VkImageLayout new_layout,
                     VkAccessFlags src_access, VkAccessFlags dst_access,
                     VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) {
	const VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = src_access,
		.dstAccessMask = dst_access,
		.oldLayout = old_layout,
		.newLayout = new_layout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = base_mip,
			.levelCount = mip_count,
			.layerCount = 1,
		},
	};
	vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace

bool initialize() {
	const VkCommandPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = context.graphics_queue_index,
	};

	if (vkCreateCommandPool(context.device, &pool_info, nullptr, &transfer_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan command pool for copies\n";
		return false;
	}
	return true;
}

void shutdown() {
	vkDestroyCommandPool(context.device, transfer_pool, nullptr);
	transfer_pool = VK_NULL_HANDLE;
}

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer) {
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
		         VMA_ALLOCATION_CREATE_MAPPED_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VmaAllocationInfo allocation = {};
	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
	                    &buffer.buffer, &buffer.allocation, &allocation) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan buffer of " << size << " bytes\n";
		return false;
	}

	buffer.mapped = allocation.pMappedData;
	buffer.size = size;
	return true;
}

void writeBuffer(const Buffer& buffer, const void* data, size_t size, size_t offset) {
	std::memcpy(static_cast<char*>(buffer.mapped) + offset, data, size);
	vmaFlushAllocation(context.allocator, buffer.allocation, offset, size);
}

void destroyBuffer(Buffer& buffer) {
	vmaDestroyBuffer(context.allocator, buffer.buffer, buffer.allocation);
	buffer = {};
}

bool createDeviceBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer) {
	Buffer staging;
	if (!createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging)) {
		return false;
	}
	writeBuffer(staging, data, size);

	// Без флагов доступа CPU и с предпочтением памяти устройства VMA выберет память GPU.
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};

	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
	                    &buffer.buffer, &buffer.allocation, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan device buffer of " << size << " bytes\n";
		destroyBuffer(staging);
		return false;
	}
	buffer.size = size;

	const bool ok = submitAndWait([&](VkCommandBuffer command_buffer) {
		const VkBufferCopy region = { .size = size };
		vkCmdCopyBuffer(command_buffer, staging.buffer, buffer.buffer, 1, &region);
	});

	destroyBuffer(staging);
	if (!ok) {
		destroyBuffer(buffer);
	}
	return ok;
}

bool createTexture(const uint8_t* rgba, uint32_t width, uint32_t height, VkFormat format, Texture& texture) {
	// Мип-цепочка строится блитами с линейной фильтрацией: формат должен это уметь, иначе остаётся один уровень.
	VkFormatProperties properties = {};
	vkGetPhysicalDeviceFormatProperties(context.physical_device, format, &properties);
	constexpr VkFormatFeatureFlags blit_features = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
	                                               VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
	const bool can_blit = (properties.optimalTilingFeatures & blit_features) == blit_features;
	const uint32_t mip_levels = can_blit ? image::mipLevelCount(width, height) : 1;

	const VkDeviceSize size = VkDeviceSize(width) * height * 4;
	Buffer staging;
	if (!createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging)) {
		return false;
	}
	writeBuffer(staging, rgba, size);

	const VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { width, height, 1 },
		.mipLevels = mip_levels,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		// TRANSFER_SRC нужен, потому что каждый уровень цепочки читается при построении следующего.
		.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};

	if (vmaCreateImage(context.allocator, &image_info, &allocation_info,
	                   &texture.image, &texture.allocation, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan image " << width << "x" << height << '\n';
		destroyBuffer(staging);
		return false;
	}
	texture.mip_levels = mip_levels;

	const bool ok = submitAndWait([&](VkCommandBuffer command_buffer) {
		constexpr VkPipelineStageFlags top = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		constexpr VkPipelineStageFlags transfer = VK_PIPELINE_STAGE_TRANSFER_BIT;
		constexpr VkPipelineStageFlags fragment = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

		// Все уровни сначала готовятся к записи, в нулевой копируются пиксели из staging buffer.
		transitionImage(command_buffer, texture.image, 0, mip_levels, VK_IMAGE_LAYOUT_UNDEFINED,
		                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, top, transfer);

		const VkBufferImageCopy region = {
			.imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
			.imageExtent = { width, height, 1 },
		};
		vkCmdCopyBufferToImage(command_buffer, staging.buffer, texture.image,
		                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		// Уровень i получается сжатием уровня i - 1 вдвое. После этого уровень i - 1 готов для шейдера.
		int32_t mip_width = int32_t(width);
		int32_t mip_height = int32_t(height);
		for (uint32_t level = 1; level < mip_levels; ++level) {
			transitionImage(command_buffer, texture.image, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
			                VK_ACCESS_TRANSFER_READ_BIT, transfer, transfer);

			const int32_t next_width = std::max(mip_width / 2, 1);
			const int32_t next_height = std::max(mip_height / 2, 1);
			const VkImageBlit blit = {
				.srcSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level - 1, .layerCount = 1 },
				.srcOffsets = { { 0, 0, 0 }, { mip_width, mip_height, 1 } },
				.dstSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level, .layerCount = 1 },
				.dstOffsets = { { 0, 0, 0 }, { next_width, next_height, 1 } },
			};
			vkCmdBlitImage(command_buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.image,
			               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

			transitionImage(command_buffer, texture.image, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
			                VK_ACCESS_SHADER_READ_BIT, transfer, fragment);

			mip_width = next_width;
			mip_height = next_height;
		}

		// Последний уровень остался в TRANSFER_DST: его тоже передаём шейдеру.
		transitionImage(command_buffer, texture.image, mip_levels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
		                VK_ACCESS_SHADER_READ_BIT, transfer, fragment);
	});

	destroyBuffer(staging);

	const VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = texture.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = mip_levels, .layerCount = 1 },
	};

	if (!ok || vkCreateImageView(context.device, &view_info, nullptr, &texture.view) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan texture\n";
		destroyTexture(texture);
		return false;
	}
	return true;
}

void destroyTexture(Texture& texture) {
	vkDestroyImageView(context.device, texture.view, nullptr);
	if (texture.image != VK_NULL_HANDLE) {
		vmaDestroyImage(context.allocator, texture.image, texture.allocation);
	}
	texture = {};
}

VkSampler createSampler() {
	const VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.minLod = 0.0f,
		.maxLod = VK_LOD_CLAMP_NONE,
	};

	VkSampler sampler = VK_NULL_HANDLE;
	if (vkCreateSampler(context.device, &sampler_info, nullptr, &sampler) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan sampler\n";
	}
	return sampler;
}

VkShaderModule loadShaderModule(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		std::cerr << "Failed to open shader " << path << " (run the program from the project root)\n";
		return VK_NULL_HANDLE;
	}

	const std::streamsize size = file.tellg();
	std::vector<uint32_t> code(static_cast<size_t>(size) / sizeof(uint32_t));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(code.data()), size);

	const VkShaderModuleCreateInfo module_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = code.size() * sizeof(uint32_t),
		.pCode = code.data(),
	};

	VkShaderModule module = VK_NULL_HANDLE;
	if (vkCreateShaderModule(context.device, &module_info, nullptr, &module) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan shader module from " << path << '\n';
		return VK_NULL_HANDLE;
	}
	return module;
}

VkPipeline createPipeline(const PipelineDesc& desc) {
	const VkPipelineShaderStageCreateInfo stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = desc.vertex_shader,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = desc.fragment_shader,
			.pName = "main",
		},
	};

	const VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &desc.vertex_binding,
		.vertexAttributeDescriptionCount = uint32_t(desc.vertex_attributes.size()),
		.pVertexAttributeDescriptions = desc.vertex_attributes.data(),
	};

	// MoltenVK не поддерживает TRIANGLE_FAN, поэтому используются только списки: треугольников или линий.
	const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = desc.topology,
	};

	// Сами viewport и scissor задаются при записи команд, поэтому конвейер не зависит от размера окна.
	const VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	// Матрица проекции переворачивает Y, поэтому треугольники, обходимые против часовой стрелки
	// снаружи фигуры, остаются такими и на экране.
	const VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	const VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};

	const VkPipelineColorBlendAttachmentState color_blend_attachment = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	const VkPipelineColorBlendStateCreateInfo color_blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attachment,
	};

	const VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]),
		.pDynamicStates = dynamic_states,
	};

	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = sizeof(stages) / sizeof(stages[0]),
		.pStages = stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth_stencil,
		.pColorBlendState = &color_blend,
		.pDynamicState = &dynamic_state,
		.layout = desc.layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};

	VkPipeline pipeline = VK_NULL_HANDLE;
	if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info,
	                              nullptr, &pipeline) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan graphics pipeline\n";
		return VK_NULL_HANDLE;
	}
	return pipeline;
}

} // namespace graphics
