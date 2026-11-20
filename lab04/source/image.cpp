#include "image.hpp"

#include <algorithm>
#include <bit>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include <stb_image.h>

namespace image {

bool load(const std::filesystem::path& path, Image& image) {
	int width = 0;
	int height = 0;
	int channels = 0;

	// Последний аргумент 4 просит всегда вернуть четыре канала: серые и RGB-картинки дополняются до RGBA.
	stbi_uc* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
	if (data == nullptr) {
		std::cerr << "Failed to load image " << path.string() << ": " << stbi_failure_reason() << '\n';
		return false;
	}

	image.width = uint32_t(width);
	image.height = uint32_t(height);
	image.pixels.assign(data, data + size_t(width) * size_t(height) * 4);
	stbi_image_free(data);
	return true;
}

Image solid(uint8_t r, uint8_t g, uint8_t b) {
	return { { r, g, b, 255 }, 1, 1 };
}

uint32_t mipLevelCount(uint32_t width, uint32_t height) {
	return uint32_t(std::bit_width(std::max({ width, height, 1u })));
}

} // namespace image
