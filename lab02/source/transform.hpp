#pragma once

#include <glm/glm.hpp>

// Матрицы хранятся по столбцам (как в GLM и GLSL), углы задаются в радианах.
// Мировая система координат правая, ось Y направлена вверх, камера смотрит вдоль -Z.
namespace transform {

glm::mat4 translate(const glm::vec3& offset);
glm::mat4 scale(const glm::vec3& factors);

glm::mat4 rotateX(float angle);
glm::mat4 rotateY(float angle);
glm::mat4 rotateZ(float angle);

glm::mat4 lookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up);

// Проекции под соглашения Vulkan: в clip space ось Y направлена вниз, глубина лежит в [0, 1].
glm::mat4 perspective(float fov_y, float aspect, float z_near, float z_far);
glm::mat4 orthographic(float left, float right, float bottom, float top, float z_near, float z_far);

} // namespace transform
