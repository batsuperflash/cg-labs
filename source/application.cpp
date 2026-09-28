#include "application.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

#include <imgui.h>

#include "geometry.hpp"
#include "graphics.hpp"
#include "transform.hpp"

namespace application {

namespace {

// Раскладка std140 должна совпадать с блоком ObjectUniforms в shaders/mesh.vert.
struct ObjectUniforms {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
	glm::vec4 color;
	glm::uvec4 flags;
};

static_assert(sizeof(ObjectUniforms) == 224);
static_assert(offsetof(ObjectUniforms, color) == 192);
static_assert(offsetof(ObjectUniforms, flags) == 208);

// У каждого объекта свой uniform buffer и свой descriptor set, который на него ссылается.
struct ObjectGpu {
	graphics::Buffer uniforms;
	VkDescriptorSet descriptor_set;
};

constexpr uint32_t max_objects = 8;

struct Camera {
	float distance = 4.0f;
	float fov_degrees = 60.0f;
	float z_near = 0.1f;
	float z_far = 100.0f;
};

graphics::Buffer vertex_buffer;
graphics::Buffer index_buffer;
uint32_t index_count;

VkDescriptorSetLayout vk_descriptor_set_layout;
VkPipelineLayout vk_pipeline_layout;
VkPipeline vk_pipeline;
VkDescriptorPool vk_descriptor_pool;

std::array<ObjectGpu, max_objects> objects_gpu;
uint32_t object_count = 1;

Camera camera;
float spin_speed = 0.6f;
float spin_angle = 0.0f;
double previous_time = -1.0;

// Если задана переменная окружения LAB_EXIT_AFTER_FRAMES, программа закрывается сама
// после указанного числа кадров. Нужно для автоматических проверок.
uint64_t exit_after_frames;
uint64_t frame_count;

uint64_t readExitAfterFrames() {
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
	const char* value = std::getenv("LAB_EXIT_AFTER_FRAMES");
	return value != nullptr ? std::strtoull(value, nullptr, 10) : 0;
}

bool createResources() {
	auto& context = graphics::internal::context;

	const geometry::Mesh mesh = geometry::makeDodecahedron();
	index_count = uint32_t(mesh.indices.size());

	const size_t vertices_size = mesh.vertices.size() * sizeof(geometry::Vertex);
	const size_t indices_size = mesh.indices.size() * sizeof(uint16_t);

	if (!graphics::createBuffer(vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer) ||
	    !graphics::createBuffer(indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer)) {
		return false;
	}

	graphics::writeBuffer(vertex_buffer, mesh.vertices.data(), vertices_size);
	graphics::writeBuffer(index_buffer, mesh.indices.data(), indices_size);

	const VkDescriptorSetLayoutBinding uniforms_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	};

	const VkDescriptorSetLayoutCreateInfo descriptor_set_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &uniforms_binding,
	};

	if (vkCreateDescriptorSetLayout(context.device, &descriptor_set_layout, nullptr,
	                                &vk_descriptor_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor set layout\n";
		return false;
	}

	const VkPipelineLayoutCreateInfo pipeline_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &vk_descriptor_set_layout,
	};

	if (vkCreatePipelineLayout(context.device, &pipeline_layout, nullptr,
	                           &vk_pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan pipeline layout\n";
		return false;
	}

	const VkVertexInputAttributeDescription vertex_attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = uint32_t(offsetof(geometry::Vertex, position)),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = uint32_t(offsetof(geometry::Vertex, color)),
		},
	};

	const VkShaderModule vertex_shader = graphics::loadShaderModule("shaders/mesh.vert.spv");
	const VkShaderModule fragment_shader = graphics::loadShaderModule("shaders/mesh.frag.spv");

	if (vertex_shader != VK_NULL_HANDLE && fragment_shader != VK_NULL_HANDLE) {
		vk_pipeline = graphics::createPipeline({
			.vertex_shader = vertex_shader,
			.fragment_shader = fragment_shader,
			.layout = vk_pipeline_layout,
			.vertex_binding = {
				.binding = 0,
				.stride = sizeof(geometry::Vertex),
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
			},
			.vertex_attributes = vertex_attributes,
		});
	}

	// После создания конвейера шейдерные модули больше не нужны.
	vkDestroyShaderModule(context.device, vertex_shader, nullptr);
	vkDestroyShaderModule(context.device, fragment_shader, nullptr);

	if (vk_pipeline == VK_NULL_HANDLE) {
		return false;
	}

	const VkDescriptorPoolSize descriptor_pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = max_objects,
	};

	const VkDescriptorPoolCreateInfo descriptor_pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = max_objects,
		.poolSizeCount = 1,
		.pPoolSizes = &descriptor_pool_size,
	};

	if (vkCreateDescriptorPool(context.device, &descriptor_pool, nullptr,
	                           &vk_descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor pool\n";
		return false;
	}

	// Ресурсы создаются сразу для всех объектов, поэтому во время работы ничего не создаётся и не удаляется.
	for (ObjectGpu& object : objects_gpu) {
		if (!graphics::createBuffer(sizeof(ObjectUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		                            object.uniforms)) {
			return false;
		}

		const VkDescriptorSetAllocateInfo descriptor_set = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = vk_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &vk_descriptor_set_layout,
		};

		if (vkAllocateDescriptorSets(context.device, &descriptor_set,
		                             &object.descriptor_set) != VK_SUCCESS) {
			std::cerr << "Failed to allocate Vulkan descriptor set\n";
			return false;
		}

		const VkDescriptorBufferInfo uniforms_info = {
			.buffer = object.uniforms.buffer,
			.offset = 0,
			.range = sizeof(ObjectUniforms),
		};

		const VkWriteDescriptorSet write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = object.descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &uniforms_info,
		};

		vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
	}

	return true;
}

} // namespace

bool initialize() {
	exit_after_frames = readExitAfterFrames();

	if (!createResources()) {
		shutdown();
		return false;
	}
	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	for (ObjectGpu& object : objects_gpu) {
		graphics::destroyBuffer(object.uniforms);
	}
	vkDestroyDescriptorPool(context.device, vk_descriptor_pool, nullptr);
	vkDestroyPipeline(context.device, vk_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, vk_pipeline_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_descriptor_set_layout, nullptr);
	graphics::destroyBuffer(index_buffer);
	graphics::destroyBuffer(vertex_buffer);
}

// Здесь меняется только состояние на CPU. Всё, что читает GPU, записывается в render():
// update() вызывается до того, как prepare() дождётся завершения предыдущего кадра.
void update(double time) {
	const float dt = previous_time < 0.0 ? 0.0f : float(std::min(time - previous_time, 0.1));
	previous_time = time;

	spin_angle += spin_speed * dt;

	ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Lab 1: regular dodecahedron");

	ImGui::Text("%.0f FPS", ImGui::GetIO().Framerate);
	ImGui::SliderFloat("Spin speed", &spin_speed, -3.0f, 3.0f, "%.2f rad/s");
	ImGui::SliderFloat("Camera distance", &camera.distance, 2.0f, 10.0f, "%.1f");
	ImGui::SliderFloat("Field of view", &camera.fov_degrees, 20.0f, 120.0f, "%.0f deg");

	ImGui::End();

	if (exit_after_frames != 0 && ++frame_count >= exit_after_frames) {
		auto* window = static_cast<GLFWwindow*>(ImGui::GetMainViewport()->PlatformHandle);
		glfwSetWindowShouldClose(window, GLFW_TRUE);
	}
}

void render(const graphics::internal::FrameData& fd) {
	if (fd.command_buffer == VK_NULL_HANDLE) {
		return;
	}

	auto& context = graphics::internal::context;
	const VkExtent2D extent = context.swapchain_extent;
	const float aspect = float(extent.width) / float(std::max(extent.height, 1u));

	// prepare() уже дождался завершения предыдущего кадра, поэтому uniform buffer можно перезаписать.
	const ObjectUniforms uniforms = {
		.model = transform::rotateY(spin_angle) * transform::rotateX(0.5f * spin_angle),
		.view = transform::lookAt(glm::vec3(0.0f, 0.0f, camera.distance), glm::vec3(0.0f),
		                          glm::vec3(0.0f, 1.0f, 0.0f)),
		.proj = transform::perspective(glm::radians(camera.fov_degrees), aspect,
		                               camera.z_near, camera.z_far),
		.color = glm::vec4(1.0f),
		.flags = glm::uvec4(1, 0, 0, 0),
	};
	graphics::writeBuffer(objects_gpu[0].uniforms, &uniforms, sizeof(uniforms));

	const VkCommandBufferBeginInfo command_buffer_begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(fd.command_buffer, &command_buffer_begin);

	const VkClearValue clear_values[] = {
		{ .color = { .float32 = { 0.02f, 0.02f, 0.03f, 1.0f } } },
		{ .depthStencil = { .depth = 1.0f, .stencil = 0 } },
	};

	// Render pass начинается каждый кадр, даже если рисовать нечего: проход ImGui ожидает,
	// что этот проход переведёт изображение в layout COLOR_ATTACHMENT_OPTIMAL.
	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .extent = extent },
		.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
		.pClearValues = clear_values,
	};

	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	const VkViewport viewport = {
		.width = float(extent.width),
		.height = float(extent.height),
		.maxDepth = 1.0f,
	};
	const VkRect2D scissor = { .extent = extent };

	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);

	const VkDeviceSize vertex_offset = 0;
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer.buffer, &vertex_offset);
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);

	for (uint32_t i = 0; i < object_count; ++i) {
		vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout,
		                        0, 1, &objects_gpu[i].descriptor_set, 0, nullptr);
		vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);
	}

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application
