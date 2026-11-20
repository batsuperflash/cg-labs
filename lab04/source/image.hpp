#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

// Загрузка растровых изображений с диска. Не зависит от Vulkan, поэтому проверяется в lab_tests.
namespace image {

// Изображение в формате RGBA8: четыре байта на пиксель, строки идут сверху вниз без выравнивания.
struct Image {
	std::vector<uint8_t> pixels;
	uint32_t width = 0;
	uint32_t height = 0;
};

// Читает файл и раскодирует его (PNG, JPEG, BMP, TGA и другие форматы stb_image) в RGBA8.
// При ошибке пишет причину в std::cerr и возвращает false.
bool load(const std::filesystem::path& path, Image& image);

// Изображение из одного пикселя. Служебные текстуры: белая вместо отсутствующего альбедо
// и чёрная вместо отсутствующих карт блика и свечения.
Image solid(uint8_t r, uint8_t g, uint8_t b);

// Число уровней мип-цепочки до размера 1 x 1: floor(log2(max(width, height))) + 1.
uint32_t mipLevelCount(uint32_t width, uint32_t height);

} // namespace image
