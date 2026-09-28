#include "transform.hpp"

#include <cmath>

namespace transform {

glm::mat4 translate(const glm::vec3& offset) {
	glm::mat4 m(1.0f);
	m[3] = glm::vec4(offset, 1.0f);
	return m;
}

glm::mat4 scale(const glm::vec3& factors) {
	glm::mat4 m(1.0f);
	m[0][0] = factors.x;
	m[1][1] = factors.y;
	m[2][2] = factors.z;
	return m;
}

// m[столбец][строка]
glm::mat4 rotateX(float angle) {
	const float c = std::cos(angle);
	const float s = std::sin(angle);

	glm::mat4 m(1.0f);
	m[1][1] = c;
	m[1][2] = s;
	m[2][1] = -s;
	m[2][2] = c;
	return m;
}

glm::mat4 rotateY(float angle) {
	const float c = std::cos(angle);
	const float s = std::sin(angle);

	glm::mat4 m(1.0f);
	m[0][0] = c;
	m[0][2] = -s;
	m[2][0] = s;
	m[2][2] = c;
	return m;
}

glm::mat4 rotateZ(float angle) {
	const float c = std::cos(angle);
	const float s = std::sin(angle);

	glm::mat4 m(1.0f);
	m[0][0] = c;
	m[0][1] = s;
	m[1][0] = -s;
	m[1][1] = c;
	return m;
}

// Строки матрицы вида — базис камеры (right, up, -forward), последний столбец переносит eye в начало координат.
glm::mat4 lookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up) {
	const glm::vec3 forward = glm::normalize(target - eye);
	const glm::vec3 right = glm::normalize(glm::cross(forward, up));
	const glm::vec3 camera_up = glm::cross(right, forward);

	glm::mat4 m(1.0f);
	for (int i = 0; i < 3; ++i) {
		m[i][0] = right[i];
		m[i][1] = camera_up[i];
		m[i][2] = -forward[i];
	}
	m[3][0] = -glm::dot(right, eye);
	m[3][1] = -glm::dot(camera_up, eye);
	m[3][2] = glm::dot(forward, eye);
	return m;
}

// x и y делятся на -z (w = -z), глубина z = -z_near переходит в 0, z = -z_far — в 1.
glm::mat4 perspective(float fov_y, float aspect, float z_near, float z_far) {
	const float f = 1.0f / std::tan(fov_y / 2.0f);

	glm::mat4 m(0.0f);
	m[0][0] = f / aspect;
	m[1][1] = -f;
	m[2][2] = z_far / (z_near - z_far);
	m[2][3] = -1.0f;
	m[3][2] = z_near * z_far / (z_near - z_far);
	return m;
}

// Параллелепипед [left, right] x [bottom, top] x [-z_near, -z_far] переходит в [-1, 1] x [1, -1] x [0, 1].
glm::mat4 orthographic(float left, float right, float bottom, float top, float z_near, float z_far) {
	glm::mat4 m(1.0f);
	m[0][0] = 2.0f / (right - left);
	m[1][1] = -2.0f / (top - bottom);
	m[2][2] = -1.0f / (z_far - z_near);
	m[3][0] = -(right + left) / (right - left);
	m[3][1] = (top + bottom) / (top - bottom);
	m[3][2] = -z_near / (z_far - z_near);
	return m;
}

} // namespace transform
