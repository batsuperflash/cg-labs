#include "scene.hpp"

#include <algorithm>
#include <cmath>

#include "transform.hpp"

namespace scene {

namespace {

constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;

// Оттенки материалов объектов.
const glm::vec3 palette[max_objects] = {
	{ 1.0f, 1.0f, 1.0f },
	{ 1.0f, 0.2f, 0.2f },
	{ 0.2f, 1.0f, 0.2f },
	{ 0.25f, 0.4f, 1.0f },
	{ 1.0f, 0.85f, 0.1f },
	{ 0.75f, 0.25f, 1.0f },
	{ 0.1f, 1.0f, 1.0f },
	{ 1.0f, 0.5f, 0.1f },
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

Object makeObject(int index) {
	Object object;
	object.animation.offset = two_pi * float(index) / float(max_objects);
	object.material.albedo = palette[index % max_objects];
	return object;
}

void addObject(Scene& scene) {
	if (scene.object_count >= max_objects) {
		return;
	}

	scene.objects[scene.object_count] = makeObject(scene.object_count);
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

Scene makeDefaultScene() {
	Scene scene;
	scene.object_count = 3;
	for (int i = 0; i < scene.object_count; ++i) {
		scene.objects[i] = makeObject(i);
		scene.objects[i].animation.offset = two_pi * float(i) / float(scene.object_count);
	}
	return scene;
}

} // namespace scene
