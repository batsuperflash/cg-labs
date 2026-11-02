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
#include "scene.hpp"
#include "ui.hpp"

namespace application {

namespace {

// Раскладка std140 должна совпадать с блоком SceneUniforms в shaders/mesh.vert и mesh.frag.
// Здесь всё общее для сцены: камера и источники света. Набор дескрипторов один на всю сцену.
struct SceneUniforms {
	glm::mat4 view;
	glm::mat4 proj;
	glm::vec4 camera_position;
	glm::vec4 ambient;       // rgb = цвет * интенсивность
	glm::vec4 sun_direction; // xyz = куда идёт свет, w = 1, если включён
	glm::vec4 sun_radiance;  // rgb = цвет * интенсивность
};

static_assert(sizeof(SceneUniforms) == 192);
static_assert(offsetof(SceneUniforms, camera_position) == 128);
static_assert(offsetof(SceneUniforms, sun_radiance) == 176);

// Раскладка std140 должна совпадать с блоком ModelUniforms. Это данные одной модели: набор дескрипторов на каждое рисование.
struct ModelUniforms {
	glm::mat4 model;
	glm::mat4 normal_matrix;
	glm::vec4 albedo;
	glm::vec4 specular; // rgb = цвет блика, w = коэффициент блика
};

static_assert(sizeof(ModelUniforms) == 160);
static_assert(offsetof(ModelUniforms, albedo) == 128);

// У каждого рисования свой uniform buffer и свой descriptor set, который на него ссылается.
struct ModelGpu {
	graphics::Buffer uniforms;
	VkDescriptorSet descriptor_set;
};

struct MeshGpu {
	graphics::Buffer vertices;
	graphics::Buffer indices;
	uint32_t index_count;
};

// Сколько моделей можно нарисовать за кадр: объекты и пол.
constexpr uint32_t max_draws = scene::max_objects + 1;

MeshGpu dodecahedron_mesh;
MeshGpu plane_mesh;

graphics::Buffer scene_uniforms;

VkDescriptorSetLayout vk_scene_set_layout;
VkDescriptorSetLayout vk_model_set_layout;
VkPipelineLayout vk_pipeline_layout;
VkPipeline vk_pipeline;
VkDescriptorPool vk_descriptor_pool;
VkDescriptorSet vk_scene_set;

std::array<ModelGpu, max_draws> models_gpu;

scene::Scene world = scene::makeDefaultScene();
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

bool createMesh(const geometry::Mesh& mesh, MeshGpu& gpu) {
	const size_t vertices_size = mesh.vertices.size() * sizeof(geometry::Vertex);
	const size_t indices_size = mesh.indices.size() * sizeof(uint16_t);

	if (!graphics::createBuffer(vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, gpu.vertices) ||
	    !graphics::createBuffer(indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, gpu.indices)) {
		return false;
	}

	graphics::writeBuffer(gpu.vertices, mesh.vertices.data(), vertices_size);
	graphics::writeBuffer(gpu.indices, mesh.indices.data(), indices_size);
	gpu.index_count = uint32_t(mesh.indices.size());
	return true;
}

void destroyMesh(MeshGpu& gpu) {
	graphics::destroyBuffer(gpu.indices);
	graphics::destroyBuffer(gpu.vertices);
}

// Записывает в набор дескрипторов ссылку на uniform buffer: слот 0 набора — это весь буфер.
void writeUniformBufferDescriptor(VkDescriptorSet set, const graphics::Buffer& buffer, VkDeviceSize range) {
	auto& context = graphics::internal::context;

	const VkDescriptorBufferInfo info = {
		.buffer = buffer.buffer,
		.offset = 0,
		.range = range,
	};

	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &info,
	};

	vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
}

bool allocateDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorSet& set) {
	auto& context = graphics::internal::context;

	const VkDescriptorSetAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = vk_descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &layout,
	};

	if (vkAllocateDescriptorSets(context.device, &allocate_info, &set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate Vulkan descriptor set\n";
		return false;
	}
	return true;
}

bool createResources() {
	auto& context = graphics::internal::context;

	if (!createMesh(geometry::makeDodecahedron(), dodecahedron_mesh) ||
	    !createMesh(geometry::makePlane(), plane_mesh)) {
		return false;
	}

	// Набор для сцены: слот 0 — uniform buffer со SceneUniforms.
	const VkDescriptorSetLayoutBinding scene_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	};

	// Набор для модели: слот 0 — uniform buffer с ModelUniforms.
	const VkDescriptorSetLayoutBinding model_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	};

	const VkDescriptorSetLayoutCreateInfo scene_set_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(scene_bindings) / sizeof(scene_bindings[0]),
		.pBindings = scene_bindings,
	};

	const VkDescriptorSetLayoutCreateInfo model_set_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(model_bindings) / sizeof(model_bindings[0]),
		.pBindings = model_bindings,
	};

	if (vkCreateDescriptorSetLayout(context.device, &scene_set_layout, nullptr,
	                                &vk_scene_set_layout) != VK_SUCCESS ||
	    vkCreateDescriptorSetLayout(context.device, &model_set_layout, nullptr,
	                                &vk_model_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor set layouts\n";
		return false;
	}

	// Номер набора в шейдере (set = 0 или 1) — это позиция его layout в этом массиве.
	const VkDescriptorSetLayout set_layouts[] = { vk_scene_set_layout, vk_model_set_layout };

	const VkPipelineLayoutCreateInfo pipeline_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = sizeof(set_layouts) / sizeof(set_layouts[0]),
		.pSetLayouts = set_layouts,
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
			.offset = uint32_t(offsetof(geometry::Vertex, normal)),
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

	// Один набор для сцены и по набору на каждое рисование модели, и во всех — по одному uniform buffer.
	const VkDescriptorPoolSize descriptor_pool_sizes[] = {
		{ .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1 + max_draws },
	};

	const VkDescriptorPoolCreateInfo descriptor_pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1 + max_draws,
		.poolSizeCount = sizeof(descriptor_pool_sizes) / sizeof(descriptor_pool_sizes[0]),
		.pPoolSizes = descriptor_pool_sizes,
	};

	if (vkCreateDescriptorPool(context.device, &descriptor_pool, nullptr,
	                           &vk_descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor pool\n";
		return false;
	}

	if (!graphics::createBuffer(sizeof(SceneUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, scene_uniforms) ||
	    !allocateDescriptorSet(vk_scene_set_layout, vk_scene_set)) {
		return false;
	}
	writeUniformBufferDescriptor(vk_scene_set, scene_uniforms, sizeof(SceneUniforms));

	// Ресурсы создаются сразу для всех рисований, поэтому во время работы ничего не создаётся и не удаляется.
	for (ModelGpu& model : models_gpu) {
		if (!graphics::createBuffer(sizeof(ModelUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, model.uniforms) ||
		    !allocateDescriptorSet(vk_model_set_layout, model.descriptor_set)) {
			return false;
		}
		writeUniformBufferDescriptor(model.descriptor_set, model.uniforms, sizeof(ModelUniforms));
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

	for (ModelGpu& model : models_gpu) {
		graphics::destroyBuffer(model.uniforms);
	}
	graphics::destroyBuffer(scene_uniforms);
	vkDestroyDescriptorPool(context.device, vk_descriptor_pool, nullptr);
	vkDestroyPipeline(context.device, vk_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, vk_pipeline_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_model_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_scene_set_layout, nullptr);
	destroyMesh(plane_mesh);
	destroyMesh(dodecahedron_mesh);
}

namespace {

// Управление камерой с клавиатуры и мыши: WASD — по горизонтали, Q и E — вниз и вверх,
// правая кнопка мыши — повернуть. Пока курсор над окном настроек или идёт ввод текста, управление выключено.
void updateCamera(float dt) {
	const ImGuiIO& io = ImGui::GetIO();

	if (!io.WantTextInput) {
		glm::vec3 move(0.0f);
		move.x += ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f;
		move.x -= ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f;
		move.y += ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f;
		move.y -= ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f;
		move.z += ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f;
		move.z -= ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f;

		if (move != glm::vec3(0.0f)) {
			scene::moveCamera(world.camera, glm::normalize(move), dt);
		}
	}

	if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		constexpr float degrees_per_pixel = 0.15f;
		scene::rotateCamera(world.camera, io.MouseDelta.x * degrees_per_pixel, -io.MouseDelta.y * degrees_per_pixel);
	}
}

} // namespace

// Здесь меняется только состояние на CPU. Всё, что читает GPU, записывается в render():
// update() вызывается до того, как prepare() дождётся завершения предыдущего кадра.
void update(double time) {
	const float dt = previous_time < 0.0 ? 0.0f : float(std::min(time - previous_time, 0.1));
	previous_time = time;

	updateCamera(dt);

	for (int i = 0; i < world.object_count; ++i) {
		scene::advance(world.objects[i].animation, dt);
	}

	ui::drawSceneWindow(world);

	if (exit_after_frames != 0 && ++frame_count >= exit_after_frames) {
		auto* window = static_cast<GLFWwindow*>(ImGui::GetMainViewport()->PlatformHandle);
		glfwSetWindowShouldClose(window, GLFW_TRUE);
	}
}

namespace {

SceneUniforms makeSceneUniforms(float aspect) {
	const scene::Camera& camera = world.camera;
	const scene::DirectionalLight& sun = world.sun;

	return {
		.view = scene::viewMatrix(camera),
		.proj = scene::projectionMatrix(camera, aspect),
		.camera_position = glm::vec4(camera.position, 1.0f),
		.ambient = glm::vec4(world.ambient.color * world.ambient.intensity, 1.0f),
		.sun_direction = glm::vec4(scene::directionFromAngles(sun.yaw_degrees, sun.pitch_degrees),
		                           sun.enabled ? 1.0f : 0.0f),
		.sun_radiance = glm::vec4(sun.color * sun.intensity, 1.0f),
	};
}

ModelUniforms makeModelUniforms(const glm::mat4& model, const glm::mat4& normal_matrix, const scene::Material& material) {
	return {
		.model = model,
		.normal_matrix = normal_matrix,
		// Цвета из ColorEdit передаются как есть: ImGui рисует свой образец в тот же sRGB-swapchain
		// без перевода, и только так цвет на экране совпадает с образцом.
		.albedo = glm::vec4(material.albedo, 1.0f),
		.specular = glm::vec4(material.specular_color, material.shininess),
	};
}

// Записывает команды для одного рисования. Каждому рисованию достаётся свой слот: свой буфер и свой набор.
// prepare() уже дождался завершения предыдущего кадра, поэтому буферы можно перезаписывать.
void drawModel(VkCommandBuffer command_buffer, uint32_t& next_slot, const MeshGpu& mesh, const ModelUniforms& uniforms) {
	const ModelGpu& slot = models_gpu[next_slot++];
	graphics::writeBuffer(slot.uniforms, &uniforms, sizeof(uniforms));

	const VkDeviceSize vertex_offset = 0;
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &mesh.vertices.buffer, &vertex_offset);
	vkCmdBindIndexBuffer(command_buffer, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT16);

	// firstSet = 1: набор модели подставляется на место set = 1 в шейдере, набор сцены (set = 0) остаётся.
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout,
	                        1, 1, &slot.descriptor_set, 0, nullptr);
	vkCmdDrawIndexed(command_buffer, mesh.index_count, 1, 0, 0, 0);
}

} // namespace

void render(const graphics::internal::FrameData& fd) {
	if (fd.command_buffer == VK_NULL_HANDLE) {
		return;
	}

	auto& context = graphics::internal::context;
	const VkExtent2D extent = context.swapchain_extent;
	const float aspect = float(extent.width) / float(std::max(extent.height, 1u));

	const SceneUniforms scene_data = makeSceneUniforms(aspect);
	graphics::writeBuffer(scene_uniforms, &scene_data, sizeof(scene_data));

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

	// Набор сцены подставляется один раз на кадр: он общий для всех моделей.
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout,
	                        0, 1, &vk_scene_set, 0, nullptr);

	uint32_t next_slot = 0;

	for (int i = 0; i < world.object_count; ++i) {
		const scene::Object& object = world.objects[i];
		drawModel(fd.command_buffer, next_slot, dodecahedron_mesh,
		          makeModelUniforms(scene::modelMatrix(object), scene::normalMatrix(object), object.material));
	}

	if (world.floor.visible) {
		// Нормаль пола всегда смотрит вверх, поэтому матрица для нормалей единичная.
		drawModel(fd.command_buffer, next_slot, plane_mesh,
		          makeModelUniforms(scene::floorMatrix(world.floor), glm::mat4(1.0f), world.floor.material));
	}

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application
