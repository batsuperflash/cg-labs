#pragma once

#include <array>
#include <numbers>

#include <glm/glm.hpp>

// Состояние сцены и вычисления над ним. Не зависит ни от Vulkan, ни от ImGui, поэтому проверяется в lab_tests.
namespace scene {

enum class Projection {
	Perspective,
	Orthographic,
};

struct Camera {
	Projection projection = Projection::Perspective;
	float distance = 8.0f;     // камера в (0, 0, distance) смотрит в начало координат
	float fov_degrees = 60.0f; // в ортографической проекции задаёт высоту видимого объёма
	float z_near = 0.1f;
	float z_far = 100.0f;
};

struct Transform {
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 rotation_degrees = glm::vec3(0.0f);
	glm::vec3 scale = glm::vec3(1.0f);
};

// Трёхмерная фигура Лиссажу: p(φ) = (R·sin(a·φ + δ), R·sin(b·φ), R·sin(c·φ)).
struct Trajectory {
	float radius = 2.0f;
	glm::ivec3 frequencies = glm::ivec3(1, 2, 3);
	float phase_shift = std::numbers::pi_v<float> / 2.0f;
};

struct Animation {
	bool playing = true;
	float speed = 0.5f;               // скорость параметра траектории, рад/с
	float spin_speed_degrees = 40.0f; // скорость собственного вращения, градусы/с
	float offset = 0.0f;              // стартовая точка объекта на траектории, рад
	float time = 0.0f;                // пройденная часть траектории, рад
	float spin_angle = 0.0f;          // рад
	Trajectory trajectory;
};

struct Object {
	Transform transform;
	Animation animation;
	glm::vec3 color = glm::vec3(1.0f); // в sRGB, как в ColorEdit
	bool vertex_colors = true;
};

constexpr int max_objects = 8;

struct Scene {
	Camera camera;
	std::array<Object, max_objects> objects = {};
	int object_count = 1;
	int selected = 0;
};

glm::vec3 trajectoryPoint(const Trajectory& trajectory, float phase);

// Сдвигает анимацию на dt секунд, если она не на паузе.
void advance(Animation& animation, float dt);

// Сначала масштаб, затем поворот из интерфейса, собственное вращение и перенос в точку траектории.
glm::mat4 modelMatrix(const Object& object);
glm::mat4 viewMatrix(const Camera& camera);
glm::mat4 projectionMatrix(const Camera& camera, float aspect);

// Новый объект стартует в своей точке траектории и получает свой оттенок.
Object makeObject(int index);
void addObject(Scene& scene);
void removeSelected(Scene& scene);

} // namespace scene
