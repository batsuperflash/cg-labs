#pragma once

#include <array>
#include <numbers>
#include <vector>

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
	glm::vec3 albedo = glm::vec3(1.0f);         // оттенок: множитель цвета текстуры (белый не меняет её)
	glm::vec3 specular_color = glm::vec3(1.0f); // цвет блика
	float shininess = 32.0f;                    // коэффициент блика: чем больше, тем меньше и резче блик
	float emissive_strength = 1.0f;             // множитель карты свечения: 0 — не светится, 1 — как в текстуре
	int texture = 0;                            // номер материала (набора текстур) в списке, который собирает приложение
};

struct Object {
	Transform transform;
	Animation animation;
	Material material;
	bool animated_sampling = false; // нетривиальное сэмплирование текстуры: несколько образцов, зависящих от времени
};

// Пол под объектами: квадрат в плоскости y = height, на нём хорошо видны свет и блики.
struct Floor {
	bool visible = true;
	float height = -3.5f;
	float half_size = 10.0f;
	float tiling = 5.0f; // сколько раз текстура повторяется вдоль каждой стороны пола
	Material material = { glm::vec3(1.0f), glm::vec3(0.25f), 16.0f };
};

// Рассеянный свет: одинаково освещает всё, чего не касается прямой свет.
struct AmbientLight {
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 0.08f;
};

// Направленный свет, как от Солнца: одно направление для всей сцены, затухания нет.
// Направление задаётся теми же углами, что и у камеры, и показывает, куда свет идёт.
struct DirectionalLight {
	bool enabled = true;
	float yaw_degrees = -35.0f;
	float pitch_degrees = -45.0f;
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 0.6f;
};

// Точечный источник: светит во все стороны из одной точки, яркость убывает как 1 / r².
struct PointLight {
	glm::vec3 position = glm::vec3(0.0f, 1.0f, 2.0f);
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 20.0f;
};

// Прожектор: точечный источник, который светит только внутри конуса. Направление задаётся теми же углами,
// что и у камеры. Углы отсчитываются от оси конуса: внутри inner свет полный, между inner и outer плавно
// затухает до нуля, за пределами outer света нет. Если углы равны, край резкий.
struct SpotLight {
	glm::vec3 position = glm::vec3(0.0f, 4.5f, 4.0f);
	float yaw_degrees = 0.0f;
	float pitch_degrees = -50.0f;
	glm::vec3 color = glm::vec3(1.0f, 0.95f, 0.8f);
	float intensity = 80.0f;
	float inner_degrees = 12.0f;
	float outer_degrees = 25.0f;
};

constexpr int max_objects = 8;
constexpr int max_point_lights = 16;
constexpr int max_spot_lights = 16;

struct Scene {
	Camera camera;
	AmbientLight ambient;
	DirectionalLight sun;
	Floor floor;
	std::array<Object, max_objects> objects = {};
	int object_count = 1;
	int selected = 0;
	std::vector<PointLight> point_lights;
	int selected_point = 0;
	std::vector<SpotLight> spot_lights;
	int selected_spot = 0;
	int material_count = 1;         // сколько материалов можно выбрать; новые объекты получают их по очереди
	bool show_light_markers = true; // яркие кубы на месте точечных и прожекторных источников
	bool show_spot_cones = true;    // конус и ось прожектора белыми линиями
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

// Новый объект стартует в своей точке траектории и получает следующий по порядку материал.
Object makeObject(int index, int material_count = 1);
void addObject(Scene& scene);
void removeSelected(Scene& scene);

// Новый источник получает свой цвет и стартует рядом с центром сцены.
void addPointLight(Scene& scene);
void removeSelectedPointLight(Scene& scene);

PointLight makePointLight(int index);

SpotLight makeSpotLight(int index);
void addSpotLight(Scene& scene);
void removeSelectedSpotLight(Scene& scene);

// Размеры значков источников света в мировых единицах.
constexpr float light_marker_size = 0.2f;  // половина стороны куба
constexpr float spot_cone_length = 3.0f;   // длина оси нарисованного конуса

// Куб [-1, 1]^3 превращается в маленький куб в точке источника.
glm::mat4 lightMarkerMatrix(const glm::vec3& position);

// Конус из makeSpotCone(): вершина в позиции прожектора, ось по направлению света,
// длина spot_cone_length, радиус основания по внешнему углу.
glm::mat4 spotConeMatrix(const SpotLight& light);

// Начальная сцена: несколько объектов на траекториях и пол.
Scene makeDefaultScene();

} // namespace scene
