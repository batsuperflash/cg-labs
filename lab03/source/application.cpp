#include "application.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

#include <imgui.h>

#include "geometry.hpp"
#include "graphics.hpp"
#include "image.hpp"
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
	glm::uvec4 counts;       // x = число точечных источников, y = число прожекторов
};

static_assert(sizeof(SceneUniforms) == 208);
static_assert(offsetof(SceneUniforms, camera_position) == 128);
static_assert(offsetof(SceneUniforms, sun_radiance) == 176);
static_assert(offsetof(SceneUniforms, counts) == 192);

// Элемент массива в storage buffer. Раскладка std430: все поля vec4, поэтому размер 32 байта без дыр.
struct PointLightGpu {
	glm::vec4 position;
	glm::vec4 radiance; // rgb = цвет * интенсивность
};

static_assert(sizeof(PointLightGpu) == 32);

struct SpotLightGpu {
	glm::vec4 position;
	glm::vec4 direction;
	glm::vec4 radiance; // rgb = цвет * интенсивность
	glm::vec4 cone;     // x = cos(внутренний угол), y = cos(внешний угол)
};

static_assert(sizeof(SpotLightGpu) == 64);

// Раскладка std140 должна совпадать с блоком ModelUniforms. Это данные одной модели. Все модели лежат в одном
// динамическом uniform buffer одна за другой с шагом model_stride, а нужная выбирается смещением при привязке набора.
struct ModelUniforms {
	glm::mat4 model;
	glm::mat4 normal_matrix;
	glm::vec4 albedo;         // множитель цвета текстуры (оттенок)
	glm::vec4 specular;       // rgb = цвет блика, w = коэффициент блика
	glm::vec4 texture_params; // xy = во сколько раз повторить текстуру по u и v
};

static_assert(sizeof(ModelUniforms) == 176);
static_assert(offsetof(ModelUniforms, albedo) == 128);
static_assert(offsetof(ModelUniforms, texture_params) == 160);

struct MeshGpu {
	graphics::Buffer vertices;
	graphics::Buffer indices;
	uint32_t index_count;
};

// Сколько моделей можно нарисовать за кадр: объекты, пол, значки всех источников и конусы прожекторов.
constexpr uint32_t max_draws = scene::max_objects + 1 + scene::max_point_lights + 2 * scene::max_spot_lights;

MeshGpu dodecahedron_mesh;
MeshGpu plane_mesh;
MeshGpu cube_mesh;
MeshGpu cone_mesh;

graphics::Buffer scene_uniforms;
graphics::Buffer model_uniforms; // max_draws элементов с шагом model_stride: один буфер на все модели
VkDeviceSize model_stride;       // размер элемента, округлённый вверх до minUniformBufferOffsetAlignment
graphics::Buffer point_lights_buffer; // место под max_point_lights элементов, используется столько, сколько источников в сцене
graphics::Buffer spot_lights_buffer;

VkDescriptorSetLayout vk_scene_set_layout;
VkDescriptorSetLayout vk_model_set_layout;
VkPipelineLayout vk_pipeline_layout;
VkPipeline vk_pipeline;      // освещаемые модели по Блинну-Фонгу
VkPipeline vk_flat_pipeline; // значки источников: треугольники одного цвета
VkPipeline vk_line_pipeline; // конусы прожекторов: линии одного цвета
VkDescriptorPool vk_descriptor_pool;
VkDescriptorSet vk_scene_set;
VkDescriptorSet vk_model_set; // динамический uniform buffer с моделями и текстура

graphics::Texture albedo_texture;
VkSampler vk_sampler;

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

	// Вершины и индексы не меняются после создания, поэтому они лежат в памяти GPU: данные копируются туда
	// через staging buffer.
	if (!graphics::createDeviceBuffer(mesh.vertices.data(), vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, gpu.vertices) ||
	    !graphics::createDeviceBuffer(mesh.indices.data(), indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, gpu.indices)) {
		return false;
	}

	gpu.index_count = uint32_t(mesh.indices.size());
	return true;
}

void destroyMesh(MeshGpu& gpu) {
	graphics::destroyBuffer(gpu.indices);
	graphics::destroyBuffer(gpu.vertices);
}

// Записывает в набор дескрипторов ссылку на буфер: слот binding набора будет указывать на этот буфер.
void writeBufferDescriptor(VkDescriptorSet set, uint32_t binding, VkDescriptorType type,
                           const graphics::Buffer& buffer, VkDeviceSize range) {
	auto& context = graphics::internal::context;

	const VkDescriptorBufferInfo info = {
		.buffer = buffer.buffer,
		.offset = 0,
		.range = range,
	};

	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set,
		.dstBinding = binding,
		.descriptorCount = 1,
		.descriptorType = type,
		.pBufferInfo = &info,
	};

	vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
}

// Записывает в набор дескрипторов текстуру вместе с сэмплером (combined image sampler).
void writeImageDescriptor(VkDescriptorSet set, uint32_t binding, VkImageView view, VkSampler sampler) {
	auto& context = graphics::internal::context;

	const VkDescriptorImageInfo info = {
		.sampler = sampler,
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set,
		.dstBinding = binding,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &info,
	};

	vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
}

bool loadTexture(const char* path, VkFormat format, graphics::Texture& texture) {
	image::Image picture;
	return image::load(path, picture) &&
	       graphics::createTexture(picture.pixels.data(), picture.width, picture.height, format, texture);
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
	    !createMesh(geometry::makePlane(), plane_mesh) || !createMesh(geometry::makeCube(), cube_mesh) ||
	    !createMesh(geometry::makeSpotCone(), cone_mesh)) {
		return false;
	}

	// Набор для сцены: слот 0 — uniform buffer со SceneUniforms, слоты 1 и 2 — storage buffers с точечными
	// и прожекторными источниками.
	const VkDescriptorSetLayoutBinding scene_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	};

	// Набор для моделей: слот 0 — динамический uniform buffer с ModelUniforms (смещение задаётся при привязке набора),
	// слот 1 — текстура вместе с сэмплером. Набор один на все модели.
	const VkDescriptorSetLayoutBinding model_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
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
		{
			.location = 2,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = uint32_t(offsetof(geometry::Vertex, uv)),
		},
	};

	const VkShaderModule vertex_shader = graphics::loadShaderModule("shaders/mesh.vert.spv");
	const VkShaderModule lit_shader = graphics::loadShaderModule("shaders/mesh.frag.spv");
	const VkShaderModule flat_shader = graphics::loadShaderModule("shaders/flat.frag.spv");

	if (vertex_shader != VK_NULL_HANDLE && lit_shader != VK_NULL_HANDLE && flat_shader != VK_NULL_HANDLE) {
		// Три конвейера отличаются фрагментным шейдером и топологией, всё остальное общее.
		graphics::PipelineDesc desc = {
			.vertex_shader = vertex_shader,
			.fragment_shader = lit_shader,
			.layout = vk_pipeline_layout,
			.vertex_binding = {
				.binding = 0,
				.stride = sizeof(geometry::Vertex),
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
			},
			.vertex_attributes = vertex_attributes,
		};

		vk_pipeline = graphics::createPipeline(desc);

		desc.fragment_shader = flat_shader;
		vk_flat_pipeline = graphics::createPipeline(desc);

		desc.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		vk_line_pipeline = graphics::createPipeline(desc);
	}

	// После создания конвейеров шейдерные модули больше не нужны.
	vkDestroyShaderModule(context.device, vertex_shader, nullptr);
	vkDestroyShaderModule(context.device, lit_shader, nullptr);
	vkDestroyShaderModule(context.device, flat_shader, nullptr);

	if (vk_pipeline == VK_NULL_HANDLE || vk_flat_pipeline == VK_NULL_HANDLE || vk_line_pipeline == VK_NULL_HANDLE) {
		return false;
	}

	// Два набора: сцена (uniform buffer и два storage buffers) и модели (динамический uniform buffer и текстура).
	const VkDescriptorPoolSize descriptor_pool_sizes[] = {
		{ .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1 },
		{ .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .descriptorCount = 1 },
		{ .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2 },
		{ .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 },
	};

	const VkDescriptorPoolCreateInfo descriptor_pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 2,
		.poolSizeCount = sizeof(descriptor_pool_sizes) / sizeof(descriptor_pool_sizes[0]),
		.pPoolSizes = descriptor_pool_sizes,
	};

	if (vkCreateDescriptorPool(context.device, &descriptor_pool, nullptr,
	                           &vk_descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor pool\n";
		return false;
	}

	constexpr VkDeviceSize point_lights_size = scene::max_point_lights * sizeof(PointLightGpu);
	constexpr VkDeviceSize spot_lights_size = scene::max_spot_lights * sizeof(SpotLightGpu);

	if (!graphics::createBuffer(sizeof(SceneUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, scene_uniforms) ||
	    !graphics::createBuffer(point_lights_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, point_lights_buffer) ||
	    !graphics::createBuffer(spot_lights_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, spot_lights_buffer) ||
	    !allocateDescriptorSet(vk_scene_set_layout, vk_scene_set)) {
		return false;
	}
	writeBufferDescriptor(vk_scene_set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, scene_uniforms, sizeof(SceneUniforms));
	writeBufferDescriptor(vk_scene_set, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, point_lights_buffer, point_lights_size);
	writeBufferDescriptor(vk_scene_set, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, spot_lights_buffer, spot_lights_size);

	// Смещение динамического uniform buffer должно быть кратно minUniformBufferOffsetAlignment устройства.
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(context.physical_device, &properties);
	const VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
	model_stride = (sizeof(ModelUniforms) + alignment - 1) / alignment * alignment;

	// Буфер создаётся сразу под все рисования, поэтому во время работы ничего не создаётся и не удаляется.
	vk_sampler = graphics::createSampler();
	if (vk_sampler == VK_NULL_HANDLE ||
	    !graphics::createBuffer(max_draws * model_stride, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, model_uniforms) ||
	    !loadTexture("textures/brick_albedo.png", VK_FORMAT_R8G8B8A8_SRGB, albedo_texture) ||
	    !allocateDescriptorSet(vk_model_set_layout, vk_model_set)) {
		return false;
	}

	// Для динамического буфера range — размер одного окна чтения: шейдер видит ModelUniforms по смещению из команды привязки.
	writeBufferDescriptor(vk_model_set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, model_uniforms, sizeof(ModelUniforms));
	writeImageDescriptor(vk_model_set, 1, albedo_texture.view, vk_sampler);

	return true;
}

} // namespace

bool initialize() {
	exit_after_frames = readExitAfterFrames();

	if (!graphics::initialize() || !createResources()) {
		shutdown();
		return false;
	}
	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	graphics::destroyTexture(albedo_texture);
	vkDestroySampler(context.device, vk_sampler, nullptr);
	graphics::destroyBuffer(model_uniforms);
	graphics::destroyBuffer(spot_lights_buffer);
	graphics::destroyBuffer(point_lights_buffer);
	graphics::destroyBuffer(scene_uniforms);
	vkDestroyDescriptorPool(context.device, vk_descriptor_pool, nullptr);
	vkDestroyPipeline(context.device, vk_line_pipeline, nullptr);
	vkDestroyPipeline(context.device, vk_flat_pipeline, nullptr);
	vkDestroyPipeline(context.device, vk_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, vk_pipeline_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_model_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_scene_set_layout, nullptr);
	destroyMesh(cone_mesh);
	destroyMesh(cube_mesh);
	destroyMesh(plane_mesh);
	destroyMesh(dodecahedron_mesh);
	graphics::shutdown();
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
		.counts = glm::uvec4(uint32_t(world.point_lights.size()), uint32_t(world.spot_lights.size()), 0u, 0u),
	};
}

// Копирует источники сцены в storage buffer. Копируется столько элементов, сколько источников сейчас в сцене.
void writeLights() {
	std::array<PointLightGpu, scene::max_point_lights> point_lights;
	for (size_t i = 0; i < world.point_lights.size(); ++i) {
		const scene::PointLight& light = world.point_lights[i];
		point_lights[i] = { glm::vec4(light.position, 1.0f), glm::vec4(light.color * light.intensity, 1.0f) };
	}

	if (!world.point_lights.empty()) {
		graphics::writeBuffer(point_lights_buffer, point_lights.data(), world.point_lights.size() * sizeof(PointLightGpu));
	}

	std::array<SpotLightGpu, scene::max_spot_lights> spot_lights;
	for (size_t i = 0; i < world.spot_lights.size(); ++i) {
		const scene::SpotLight& light = world.spot_lights[i];
		spot_lights[i] = {
			glm::vec4(light.position, 1.0f),
			glm::vec4(scene::directionFromAngles(light.yaw_degrees, light.pitch_degrees), 0.0f),
			glm::vec4(light.color * light.intensity, 1.0f),
			glm::vec4(std::cos(glm::radians(light.inner_degrees)), std::cos(glm::radians(light.outer_degrees)), 0.0f, 0.0f),
		};
	}

	if (!world.spot_lights.empty()) {
		graphics::writeBuffer(spot_lights_buffer, spot_lights.data(), world.spot_lights.size() * sizeof(SpotLightGpu));
	}
}

ModelUniforms makeModelUniforms(const glm::mat4& model, const glm::mat4& normal_matrix, const scene::Material& material,
                                const glm::vec2& uv_scale = glm::vec2(1.0f)) {
	return {
		.model = model,
		.normal_matrix = normal_matrix,
		// Цвета из ColorEdit передаются как есть: ImGui рисует свой образец в тот же sRGB-swapchain
		// без перевода, и только так цвет на экране совпадает с образцом.
		.albedo = glm::vec4(material.albedo, 1.0f),
		.specular = glm::vec4(material.specular_color, material.shininess),
		.texture_params = glm::vec4(uv_scale, 0.0f, 0.0f),
	};
}

// Записывает команды для одного рисования. Данные модели пишутся в свой элемент динамического буфера,
// а при привязке единственного набора дескрипторов задаётся смещение к этому элементу.
// prepare() уже дождался завершения предыдущего кадра, поэтому буфер можно перезаписывать.
void drawModel(VkCommandBuffer command_buffer, uint32_t& next_slot, const MeshGpu& mesh, const ModelUniforms& uniforms) {
	const uint32_t dynamic_offset = uint32_t(next_slot++ * model_stride);
	graphics::writeBuffer(model_uniforms, &uniforms, sizeof(uniforms), dynamic_offset);

	const VkDeviceSize vertex_offset = 0;
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &mesh.vertices.buffer, &vertex_offset);
	vkCmdBindIndexBuffer(command_buffer, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT16);

	// firstSet = 1: набор моделей подставляется на место set = 1 в шейдере, набор сцены (set = 0) остаётся.
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout,
	                        1, 1, &vk_model_set, 1, &dynamic_offset);
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
	writeLights();

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

	// Набор сцены подставляется один раз на кадр: он общий для всех моделей.
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout,
	                        0, 1, &vk_scene_set, 0, nullptr);

	uint32_t next_slot = 0;

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);

	for (int i = 0; i < world.object_count; ++i) {
		const scene::Object& object = world.objects[i];
		drawModel(fd.command_buffer, next_slot, dodecahedron_mesh,
		          makeModelUniforms(scene::modelMatrix(object), scene::normalMatrix(object), object.material));
	}

	if (world.floor.visible) {
		// Нормаль пола всегда смотрит вверх, поэтому матрица для нормалей единичная.
		drawModel(fd.command_buffer, next_slot, plane_mesh,
		          makeModelUniforms(scene::floorMatrix(world.floor), glm::mat4(1.0f), world.floor.material,
		                                  glm::vec2(world.floor.tiling)));
	}

	// Значки источников не освещаются: другой конвейер выводит цвет источника как есть.
	if (world.show_light_markers) {
		vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_flat_pipeline);

		auto drawMarker = [&](const glm::vec3& position, const glm::vec3& color) {
			const scene::Material marker = { color, glm::vec3(0.0f), 1.0f };
			drawModel(fd.command_buffer, next_slot, cube_mesh,
			          makeModelUniforms(scene::lightMarkerMatrix(position), glm::mat4(1.0f), marker));
		};

		for (const scene::PointLight& light : world.point_lights) {
			drawMarker(light.position, light.color);
		}
		for (const scene::SpotLight& light : world.spot_lights) {
			drawMarker(light.position, light.color);
		}
	}

	// Контур прожектора рисуется белыми линиями.
	if (world.show_spot_cones) {
		vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_line_pipeline);

		const scene::Material white = { glm::vec3(1.0f), glm::vec3(0.0f), 1.0f };
		for (const scene::SpotLight& light : world.spot_lights) {
			drawModel(fd.command_buffer, next_slot, cone_mesh,
			          makeModelUniforms(scene::spotConeMatrix(light), glm::mat4(1.0f), white));
		}
	}

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application
