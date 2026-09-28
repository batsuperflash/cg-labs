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

// Буфер в памяти, доступной CPU, постоянно отображённый в адресное пространство процесса.
bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer);
void writeBuffer(const Buffer& buffer, const void* data, size_t size);
void destroyBuffer(Buffer& buffer);

// Загружает SPIR-V по пути относительно рабочей директории. При ошибке возвращает VK_NULL_HANDLE.
VkShaderModule loadShaderModule(const char* path);

struct PipelineDesc {
	VkShaderModule vertex_shader;
	VkShaderModule fragment_shader;
	VkPipelineLayout layout;
	VkVertexInputBindingDescription vertex_binding;
	std::span<const VkVertexInputAttributeDescription> vertex_attributes;
};

// Конвейер для треугольников в основном render pass: тест глубины, отсечение задних граней,
// динамические viewport и scissor. При ошибке возвращает VK_NULL_HANDLE.
VkPipeline createPipeline(const PipelineDesc& desc);

} // namespace graphics
