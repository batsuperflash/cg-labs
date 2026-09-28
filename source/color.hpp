#pragma once

#include <cmath>

#include <glm/glm.hpp>

// Процедурные цвета задаются в sRGB, а шейдер выводит линейные значения:
// swapchain в формате *_SRGB сам переводит результат в sRGB при записи.
namespace color {

inline float srgbToLinear(float value) {
	return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

inline glm::vec3 srgbToLinear(const glm::vec3& value) {
	return glm::vec3(srgbToLinear(value.r), srgbToLinear(value.g), srgbToLinear(value.b));
}

} // namespace color
