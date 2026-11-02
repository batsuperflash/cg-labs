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

// Камера свободно летает: положение и два угла. При yaw = 0 и pitch = 0 она смотрит вдоль -Z.
struct Camera {
	Projection projection = Projection::Perspective;
	glm::vec3 position = glm::vec3(0.0f, 3.0f, 11.0f);
	float yaw_degrees = 0.0f;    // поворот вокруг оси Y, положительный — направо
	float pitch_degrees = -12.0f; // наклон вверх (+) или вниз (-)
	float fov_degrees = 60.0f;   // в ортографической проекции задаёт высоту видимого объёма
	float z_near = 0.1f;
	float z_far = 100.0f;
	float move_speed = 5.0f;     // единиц в секунду при управлении с клавиатуры
	float focus_distance = 11.0f; // только для ортографической проекции: на каком расстоянии измеряется высота объёма
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

// Параметры материала по модели Блинна-Фонга.
struct Material {
	glm::vec3 albedo = glm::vec3(0.8f);         // цвет рассеянного отражения
	glm::vec3 specular_color = glm::vec3(1.0f); // цвет блика
	float shininess = 32.0f;                    // коэффициент блика: чем больше, тем меньше и резче блик
};

struct Object {
	Transform transform;
	Animation animation;
	Material material;
};

// Пол под объектами: квадрат в плоскости y = height, на нём хорошо видны свет и блики.
struct Floor {
	bool visible = true;
	float height = -3.5f;
	float half_size = 10.0f;
	Material material = { glm::vec3(0.5f, 0.5f, 0.55f), glm::vec3(0.25f), 16.0f };
};

// Рассеянный свет: одинаково освещает всё, чего не касается прямой свет.
struct AmbientLight {
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 0.15f;
};

// Направленный свет, как от Солнца: одно направление для всей сцены, затухания нет.
// Направление задаётся теми же углами, что и у камеры, и показывает, куда свет идёт.
struct DirectionalLight {
	bool enabled = true;
	float yaw_degrees = -35.0f;
	float pitch_degrees = -45.0f;
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 1.0f;
};

constexpr int max_objects = 8;

struct Scene {
	Camera camera;
	AmbientLight ambient;
	DirectionalLight sun;
	Floor floor;
	std::array<Object, max_objects> objects = {};
	int object_count = 1;
	int selected = 0;
};

// Единичный вектор по углам: yaw = 0, pitch = 0 даёт (0, 0, -1), положительный pitch поднимает вектор вверх.
glm::vec3 directionFromAngles(float yaw_degrees, float pitch_degrees);

glm::vec3 cameraForward(const Camera& camera);

// Сдвигает камеру: local.x — вправо, local.y — вверх по миру, local.z — вперёд, куда смотрит камера.
void moveCamera(Camera& camera, const glm::vec3& local, float dt);

// Поворачивает камеру; угол наклона не даёт перевернуться через верх.
void rotateCamera(Camera& camera, float delta_yaw_degrees, float delta_pitch_degrees);

glm::vec3 trajectoryPoint(const Trajectory& trajectory, float phase);

// Сдвигает анимацию на dt секунд, если она не на паузе.
void advance(Animation& animation, float dt);

// Сначала масштаб, затем поворот из интерфейса, собственное вращение и перенос в точку траектории.
glm::mat4 modelMatrix(const Object& object);

// Матрица для нормалей: обратная транспонированная от матрицы модели. Поворот сохраняется,
// а масштаб заменяется на обратный, чтобы при растяжении нормаль оставалась перпендикулярной поверхности.
glm::mat4 normalMatrix(const Object& object);

glm::mat4 viewMatrix(const Camera& camera);
glm::mat4 projectionMatrix(const Camera& camera, float aspect);

glm::mat4 floorMatrix(const Floor& floor);

// Новый объект стартует в своей точке траектории и получает свой оттенок.
Object makeObject(int index);
void addObject(Scene& scene);
void removeSelected(Scene& scene);

// Начальная сцена: несколько объектов на траекториях и пол.
Scene makeDefaultScene();

} // namespace scene
