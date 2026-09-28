#include "ui.hpp"

#include <imgui.h>

namespace ui {

namespace {

void drawCameraSection(scene::Camera& camera) {
	if (!ImGui::CollapsingHeader("Camera & projection", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	if (ImGui::RadioButton("Perspective", camera.projection == scene::Projection::Perspective)) {
		camera.projection = scene::Projection::Perspective;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Orthographic", camera.projection == scene::Projection::Orthographic)) {
		camera.projection = scene::Projection::Orthographic;
	}

	ImGui::SliderFloat("Field of view", &camera.fov_degrees, 20.0f, 120.0f, "%.0f deg");
	ImGui::SliderFloat("Camera distance", &camera.distance, 3.0f, 15.0f, "%.1f");
	ImGui::DragFloat("Near plane", &camera.z_near, 0.01f, 0.01f, camera.z_far - 0.01f, "%.2f",
	                 ImGuiSliderFlags_AlwaysClamp);
	ImGui::DragFloat("Far plane", &camera.z_far, 0.5f, camera.z_near + 0.01f, 500.0f, "%.1f",
	                 ImGuiSliderFlags_AlwaysClamp);

	if (camera.projection == scene::Projection::Orthographic) {
		ImGui::TextDisabled("View height = 2 * distance * tan(FOV / 2)");
	}
}

void drawTransformSection(scene::Transform& transform) {
	if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	// Отрицательный масштаб запрещён: он выворачивает треугольники, и отсечение задних граней скрыло бы лицевые.
	ImGui::DragFloat3("Position", &transform.position.x, 0.01f, -5.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::DragFloat3("Rotation", &transform.rotation_degrees.x, 0.5f, -180.0f, 180.0f, "%.0f deg",
	                  ImGuiSliderFlags_AlwaysClamp);
	ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, 0.05f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

	if (ImGui::Button("Reset transform")) {
		transform = {};
	}
}

void drawAnimationSection(scene::Animation& animation) {
	if (!ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	// После ### идёт идентификатор кнопки: он не меняется, когда меняется подпись.
	if (ImGui::Button(animation.playing ? "Pause###playback" : "Play###playback")) {
		animation.playing = !animation.playing;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset animation")) {
		animation.time = 0.0f;
		animation.spin_angle = 0.0f;
	}

	ImGui::SliderFloat("Speed", &animation.speed, 0.0f, 3.0f, "%.2f rad/s");
	ImGui::SliderFloat("Spin speed", &animation.spin_speed_degrees, -360.0f, 360.0f, "%.0f deg/s");

	scene::Trajectory& trajectory = animation.trajectory;
	ImGui::SliderFloat("Radius", &trajectory.radius, 0.0f, 3.0f, "%.2f");
	ImGui::SliderInt3("Frequencies", &trajectory.frequencies.x, 1, 5);
	ImGui::SliderAngle("Phase shift", &trajectory.phase_shift, 0.0f, 360.0f);
	ImGui::TextDisabled("p(t) = R (sin(a t + shift), sin(b t), sin(c t))");
}

} // namespace

void drawSceneWindow(scene::Scene& scene) {
	ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Lab 1: regular dodecahedron", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::PushItemWidth(220.0f);

	ImGui::Text("%.0f FPS", ImGui::GetIO().Framerate);
	drawCameraSection(scene.camera);

	scene::Object& object = scene.objects[scene.selected];
	drawTransformSection(object.transform);
	drawAnimationSection(object.animation);

	ImGui::PopItemWidth();
	ImGui::End();
}

} // namespace ui
