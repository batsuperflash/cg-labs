#include "scene.hpp"

#include <algorithm>
#include <cmath>

#include "transform.hpp"

namespace scene {

namespace {

constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;

// Оттенки объектов в sRGB. Первый белый, чтобы цвета вершин были видны без изменений.
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

glm::mat4 viewMatrix(const Camera& camera) {
	return transform::lookAt(glm::vec3(0.0f, 0.0f, camera.distance), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 projectionMatrix(const Camera& camera, float aspect) {
	const float fov = glm::radians(camera.fov_degrees);
	if (camera.projection == Projection::Perspective) {
		return transform::perspective(fov, aspect, camera.z_near, camera.z_far);
	}

	// Высота объёма равна высоте, которую перспективная камера видит в плоскости z = 0,
	// поэтому при переключении проекции размер фигуры в центре сцены не меняется.
	const float height = 2.0f * camera.distance * std::tan(fov / 2.0f);
	const float width = height * aspect;
	return transform::orthographic(-width / 2.0f, width / 2.0f, -height / 2.0f, height / 2.0f,
	                               camera.z_near, camera.z_far);
}

Object makeObject(int index) {
	Object object;
	object.animation.offset = two_pi * float(index) / float(max_objects);
	object.color = palette[index % max_objects];
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

} // namespace scene
