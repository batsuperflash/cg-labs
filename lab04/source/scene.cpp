#include "scene.hpp"

#include <algorithm>
#include <cmath>

#include "transform.hpp"

namespace scene {

namespace {

constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;

// Цвета новых источников света.
const glm::vec3 light_palette[] = {
	{ 1.0f, 0.35f, 0.25f },
	{ 0.3f, 0.5f, 1.0f },
	{ 1.0f, 0.9f, 0.4f },
	{ 0.4f, 1.0f, 0.5f },
	{ 1.0f, 0.4f, 0.9f },
	{ 0.4f, 1.0f, 1.0f },
};

} // namespace

glm::vec3 directionFromAngles(float yaw_degrees, float pitch_degrees) {
	const float yaw = glm::radians(yaw_degrees);
	const float pitch = glm::radians(pitch_degrees);
	return glm::vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
}

glm::vec3 cameraForward(const Camera& camera) {
	return directionFromAngles(camera.yaw_degrees, camera.pitch_degrees);
}

void moveCamera(Camera& camera, const glm::vec3& local, float dt) {
	const glm::vec3 up(0.0f, 1.0f, 0.0f);
	const glm::vec3 forward = cameraForward(camera);
	const glm::vec3 right = glm::normalize(glm::cross(forward, up));

	camera.position += (right * local.x + up * local.y + forward * local.z) * (camera.move_speed * dt);
}

void rotateCamera(Camera& camera, float delta_yaw_degrees, float delta_pitch_degrees) {
	camera.yaw_degrees = std::remainder(camera.yaw_degrees + delta_yaw_degrees, 360.0f);
	camera.pitch_degrees = std::clamp(camera.pitch_degrees + delta_pitch_degrees, -89.0f, 89.0f);
}

glm::vec3 trajectoryPoint(const Trajectory& trajectory, float phase) {
	const glm::vec3 frequencies(trajectory.frequencies);
	return trajectory.radius * glm::vec3(std::sin(frequencies.x * phase + trajectory.phase_shift),
	                                     std::sin(frequencies.y * phase),
	                                     std::sin(frequencies.z * phase));
}

void advance(Animation& animation, float dt) {
	if (!animation.playing) {
		return;
	}

	// При целых частотах траектория периодична с периодом 2π, поэтому значения можно держать в [0, 2π):
	// так float не теряет точность даже после долгой работы программы.
	animation.time = std::fmod(animation.time + dt * animation.speed, two_pi);
	animation.spin_angle = std::fmod(animation.spin_angle + dt * glm::radians(animation.spin_speed_degrees), two_pi);
}

glm::mat4 modelMatrix(const Object& object) {
	const Animation& animation = object.animation;
	const glm::vec3 angles = glm::radians(object.transform.rotation_degrees);

	const glm::mat4 scaling = transform::scale(object.transform.scale);
	const glm::mat4 rotation = transform::rotateZ(angles.z) * transform::rotateY(angles.y) *
	                           transform::rotateX(angles.x);
	const glm::mat4 spin = transform::rotateY(animation.spin_angle) * transform::rotateX(0.5f * animation.spin_angle);
	const glm::mat4 translation = transform::translate(
		object.transform.position + trajectoryPoint(animation.trajectory, animation.offset + animation.time));

	return translation * spin * rotation * scaling;
}

glm::mat4 normalMatrix(const Object& object) {
	const glm::vec3 angles = glm::radians(object.transform.rotation_degrees);
	const Animation& animation = object.animation;

	const glm::mat4 rotation = transform::rotateZ(angles.z) * transform::rotateY(angles.y) *
	                           transform::rotateX(angles.x);
	const glm::mat4 spin = transform::rotateY(animation.spin_angle) * transform::rotateX(0.5f * animation.spin_angle);

	return spin * rotation * transform::scale(1.0f / object.transform.scale);
}

glm::mat4 viewMatrix(const Camera& camera) {
	return transform::lookAt(camera.position, camera.position + cameraForward(camera), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 projectionMatrix(const Camera& camera, float aspect) {
	const float fov = glm::radians(camera.fov_degrees);
	if (camera.projection == Projection::Perspective) {
		return transform::perspective(fov, aspect, camera.z_near, camera.z_far);
	}

	// Высота объёма равна высоте, которую перспективная камера видит на расстоянии focus_distance,
	// поэтому при переключении проекции размер фигуры на этом расстоянии не меняется.
	const float height = 2.0f * camera.focus_distance * std::tan(fov / 2.0f);
	const float width = height * aspect;
	return transform::orthographic(-width / 2.0f, width / 2.0f, -height / 2.0f, height / 2.0f,
	                               camera.z_near, camera.z_far);
}

glm::mat4 floorMatrix(const Floor& floor) {
	return transform::translate(glm::vec3(0.0f, floor.height, 0.0f)) *
	       transform::scale(glm::vec3(floor.half_size, 1.0f, floor.half_size));
}

Object makeObject(int index, int material_count) {
	Object object;
	object.animation.offset = two_pi * float(index) / float(max_objects);
	object.material.texture = index % std::max(material_count, 1);
	return object;
}

void addObject(Scene& scene) {
	if (scene.object_count >= max_objects) {
		return;
	}

	scene.objects[scene.object_count] = makeObject(scene.object_count, scene.material_count);
	scene.selected = scene.object_count;
	++scene.object_count;
}

void removeSelected(Scene& scene) {
	if (scene.object_count <= 1) {
		return;
	}

	std::move(scene.objects.begin() + scene.selected + 1, scene.objects.begin() + scene.object_count,
	          scene.objects.begin() + scene.selected);
	--scene.object_count;
	scene.selected = std::min(scene.selected, scene.object_count - 1);
}

PointLight makePointLight(int index) {
	constexpr int palette_size = sizeof(light_palette) / sizeof(light_palette[0]);

	PointLight light;
	light.color = light_palette[index % palette_size];
	light.position = glm::vec3(-4.0f + 2.0f * float(index % 5), 1.0f, 2.0f);
	return light;
}

void addPointLight(Scene& scene) {
	if (int(scene.point_lights.size()) >= max_point_lights) {
		return;
	}

	scene.point_lights.push_back(makePointLight(int(scene.point_lights.size())));
	scene.selected_point = int(scene.point_lights.size()) - 1;
}

void removeSelectedPointLight(Scene& scene) {
	if (scene.point_lights.empty()) {
		return;
	}

	scene.point_lights.erase(scene.point_lights.begin() + scene.selected_point);
	scene.selected_point = std::clamp(scene.selected_point, 0, std::max(int(scene.point_lights.size()) - 1, 0));
}

SpotLight makeSpotLight(int index) {
	constexpr int palette_size = sizeof(light_palette) / sizeof(light_palette[0]);

	SpotLight light;
	light.color = index == 0 ? light.color : light_palette[(index + 2) % palette_size];
	light.position = glm::vec3(-4.0f + 2.0f * float(index % 5), 4.5f, 4.0f);
	return light;
}

void addSpotLight(Scene& scene) {
	if (int(scene.spot_lights.size()) >= max_spot_lights) {
		return;
	}

	scene.spot_lights.push_back(makeSpotLight(int(scene.spot_lights.size())));
	scene.selected_spot = int(scene.spot_lights.size()) - 1;
}

void removeSelectedSpotLight(Scene& scene) {
	if (scene.spot_lights.empty()) {
		return;
	}

	scene.spot_lights.erase(scene.spot_lights.begin() + scene.selected_spot);
	scene.selected_spot = std::clamp(scene.selected_spot, 0, std::max(int(scene.spot_lights.size()) - 1, 0));
}

glm::mat4 lightMarkerMatrix(const glm::vec3& position) {
	return transform::translate(position) * transform::scale(glm::vec3(light_marker_size));
}

glm::mat4 spotConeMatrix(const SpotLight& light) {
	const float radius = spot_cone_length * std::tan(glm::radians(light.outer_degrees));

	// Поворот, который переводит -Z в направление света: сначала наклон pitch вокруг X, потом поворот вокруг Y.
	const glm::mat4 orientation = transform::rotateY(-glm::radians(light.yaw_degrees)) *
	                              transform::rotateX(glm::radians(light.pitch_degrees));

	return transform::translate(light.position) * orientation *
	       transform::scale(glm::vec3(radius, radius, spot_cone_length));
}

Scene makeDefaultScene() {
	Scene scene;
	scene.object_count = 3;
	for (int i = 0; i < scene.object_count; ++i) {
		scene.objects[i] = makeObject(i);
		scene.objects[i].animation.offset = two_pi * float(i) / float(scene.object_count);
	}

	// Нетривиальное сэмплирование включено у одного объекта.
	scene.objects[1].animated_sampling = true;

	scene.point_lights = {
		{ glm::vec3(-5.0f, 0.5f, 2.0f), light_palette[0], 25.0f },
		{ glm::vec3(5.0f, -0.5f, 1.0f), light_palette[1], 25.0f },
	};
	scene.spot_lights = { SpotLight() };
	return scene;
}

} // namespace scene
