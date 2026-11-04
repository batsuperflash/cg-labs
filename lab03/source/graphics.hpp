#pragma once

#include <cstddef>
#include <span>

#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

namespace graphics {

struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = nullptr;
	void* mapped = nullptr;
	VkDeviceSize size = 0;
};

// Создаёт и освобождает общие для работы ресурсы: пул команд для разовых копирований в память GPU.
// Вызывается после graphics::internal::initialize и до shutdown.
bool initialize();
void shutdown();

// Буфер в памяти, доступной CPU, постоянно отображённый в адресное пространство процесса.
// Подходит для данных, которые меняются каждый кадр (uniform buffers).
bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer);
void writeBuffer(const Buffer& buffer, const void* data, size_t size, size_t offset = 0);
void destroyBuffer(Buffer& buffer);

// Буфер в памяти GPU (VRAM), недоступной CPU. Данные попадают в него через временный staging buffer
// и команду vkCmdCopyBuffer; функция возвращается, когда копирование закончилось.
// Подходит для данных, которые не меняются: вершины и индексы.
bool createDeviceBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer);

struct Texture {
	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = nullptr;
	VkImageView view = VK_NULL_HANDLE;
	uint32_t mip_levels = 1;
};

// Текстура в VRAM из пикселей RGBA8 (строки сверху вниз). Пиксели копируются через staging buffer, затем на GPU
// строится вся мип-цепочка блитами с линейной фильтрацией. Формат определяет, как шейдер читает цвет:
// VK_FORMAT_R8G8B8A8_SRGB переводит значения из sRGB в линейные (для цветных изображений),
// VK_FORMAT_R8G8B8A8_UNORM отдаёт байты как есть (для данных вроде карты блика).
bool createTexture(const uint8_t* rgba, uint32_t width, uint32_t height, VkFormat format, Texture& texture);
void destroyTexture(Texture& texture);

// Сэмплер для всех текстур: линейная фильтрация, переход между мип-уровнями, повторение при выходе за [0, 1].
VkSampler createSampler();

// Загружает SPIR-V по пути относительно рабочей директории. При ошибке возвращает VK_NULL_HANDLE.
VkShaderModule loadShaderModule(const char* path);

struct PipelineDesc {
	VkShaderModule vertex_shader;
	VkShaderModule fragment_shader;
	VkPipelineLayout layout;
	VkVertexInputBindingDescription vertex_binding;
	std::span<const VkVertexInputAttributeDescription> vertex_attributes;
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
};

// Конвейер для примитивов заданной топологии (по умолчанию треугольники) в основном render pass: тест глубины, отсечение задних граней,
// динамические viewport и scissor. При ошибке возвращает VK_NULL_HANDLE.
VkPipeline createPipeline(const PipelineDesc& desc);

} // namespace graphics
