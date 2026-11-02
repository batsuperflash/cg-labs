#include "ui.hpp"

#include <cstdio>

#include <imgui.h>

namespace ui {

namespace {

void drawObjectsSection(scene::Scene& scene) {
	if (!ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	// Выпадающий список занимает одну строку, поэтому окно не растёт с числом объектов.
	auto drawSwatch = [](const glm::vec3& color) {
		const float size = ImGui::GetFrameHeight();
		ImGui::ColorButton("##swatch", ImVec4(color.r, color.g, color.b, 1.0f), ImGuiColorEditFlags_NoTooltip,
		                   ImVec2(size, size));
		ImGui::SameLine();
	};

	char preview[32];
	std::snprintf(preview, sizeof(preview), "Object %d", scene.selected + 1);
	drawSwatch(scene.objects[scene.selected].material.albedo);
	if (ImGui::BeginCombo("Selected", preview)) {
		for (int i = 0; i < scene.object_count; ++i) {
			char label[32];
			std::snprintf(label, sizeof(label), "Object %d", i + 1);

			ImGui::PushID(i);
			drawSwatch(scene.objects[i].material.albedo);
			if (ImGui::Selectable(label, scene.selected == i)) {
				scene.selected = i;
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	ImGui::BeginDisabled(scene.object_count >= scene::max_objects);
	if (ImGui::Button("Add")) {
		scene::addObject(scene);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(scene.object_count <= 1);
	if (ImGui::Button("Remove")) {
		scene::removeSelected(scene);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::TextDisabled("%d of %d", scene.object_count, scene::max_objects);
}

void drawCameraSection(scene::Camera& camera) {
	if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::DragFloat3("Position##camera", &camera.position.x, 0.05f, -50.0f, 50.0f, "%.2f");
	ImGui::SliderFloat("Yaw", &camera.yaw_degrees, -180.0f, 180.0f, "%.0f deg");
	ImGui::SliderFloat("Pitch", &camera.pitch_degrees, -89.0f, 89.0f, "%.0f deg");
	ImGui::SliderFloat("Field of view", &camera.fov_degrees, 20.0f, 120.0f, "%.0f deg");
	ImGui::SliderFloat("Move speed", &camera.move_speed, 1.0f, 20.0f, "%.1f");

	if (ImGui::RadioButton("Perspective", camera.projection == scene::Projection::Perspective)) {
		camera.projection = scene::Projection::Perspective;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Orthographic", camera.projection == scene::Projection::Orthographic)) {
		camera.projection = scene::Projection::Orthographic;
	}

	ImGui::DragFloat("Near plane", &camera.z_near, 0.01f, 0.01f, camera.z_far - 0.01f, "%.2f",
	                 ImGuiSliderFlags_AlwaysClamp);
	ImGui::DragFloat("Far plane", &camera.z_far, 0.5f, camera.z_near + 0.01f, 500.0f, "%.1f",
	                 ImGuiSliderFlags_AlwaysClamp);

	if (camera.projection == scene::Projection::Orthographic) {
		ImGui::SliderFloat("Focus distance", &camera.focus_distance, 3.0f, 30.0f, "%.1f");
		ImGui::TextDisabled("View height = 2 * focus * tan(FOV / 2)");
	}

	ImGui::TextDisabled("WASD move, Q/E down/up, right mouse button look");
	if (ImGui::Button("Reset camera")) {
		camera = {};
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

void drawMaterialSection(scene::Material& material, const char* title) {
	if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::ColorEdit3("Albedo", &material.albedo.x);
	ImGui::ColorEdit3("Specular color", &material.specular_color.x);
	ImGui::SliderFloat("Shininess", &material.shininess, 1.0f, 256.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
}

void drawLightingSection(scene::Scene& scene) {
	if (!ImGui::CollapsingHeader("Ambient & directional light", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::ColorEdit3("Ambient color", &scene.ambient.color.x);
	ImGui::SliderFloat("Ambient intensity", &scene.ambient.intensity, 0.0f, 1.0f, "%.2f");

	ImGui::Separator();

	scene::DirectionalLight& sun = scene.sun;
	ImGui::Checkbox("Directional light", &sun.enabled);
	ImGui::SliderFloat("Light yaw", &sun.yaw_degrees, -180.0f, 180.0f, "%.0f deg");
	ImGui::SliderFloat("Light pitch", &sun.pitch_degrees, -89.0f, 89.0f, "%.0f deg");
	ImGui::ColorEdit3("Light color", &sun.color.x);
	ImGui::SliderFloat("Light intensity", &sun.intensity, 0.0f, 5.0f, "%.2f");
	ImGui::TextDisabled("Yaw and pitch give the direction the light travels");
}

void drawPointLightsSection(scene::Scene& scene) {
	if (!ImGui::CollapsingHeader("Point lights", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	auto drawSwatch = [](const glm::vec3& color) {
		const float size = ImGui::GetFrameHeight();
		ImGui::ColorButton("##swatch", ImVec4(color.r, color.g, color.b, 1.0f), ImGuiColorEditFlags_NoTooltip,
		                   ImVec2(size, size));
		ImGui::SameLine();
	};

	const int count = int(scene.point_lights.size());

	if (count > 0) {
		char preview[32];
		std::snprintf(preview, sizeof(preview), "Point light %d", scene.selected_point + 1);
		drawSwatch(scene.point_lights[scene.selected_point].color);
		if (ImGui::BeginCombo("Selected##point", preview)) {
			for (int i = 0; i < count; ++i) {
				char label[32];
				std::snprintf(label, sizeof(label), "Point light %d", i + 1);

				ImGui::PushID(i);
				drawSwatch(scene.point_lights[i].color);
				if (ImGui::Selectable(label, scene.selected_point == i)) {
					scene.selected_point = i;
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
	}

	ImGui::BeginDisabled(count >= scene::max_point_lights);
	if (ImGui::Button("Add##point")) {
		scene::addPointLight(scene);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(count == 0);
	if (ImGui::Button("Remove##point")) {
		scene::removeSelectedPointLight(scene);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::TextDisabled("%d of %d", count, scene::max_point_lights);

	if (scene.point_lights.empty()) {
		return;
	}

	scene::PointLight& light = scene.point_lights[scene.selected_point];
	ImGui::DragFloat3("Position##point", &light.position.x, 0.05f, -20.0f, 20.0f, "%.2f");
	ImGui::ColorEdit3("Color##point", &light.color.x);
	ImGui::SliderFloat("Intensity##point", &light.intensity, 0.0f, 200.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
	ImGui::TextDisabled("Brightness falls off as intensity / distance^2");
}

void drawFloorSection(scene::Floor& floor) {
	if (!ImGui::CollapsingHeader("Floor")) {
		return;
	}

	ImGui::Checkbox("Show floor", &floor.visible);
	ImGui::SliderFloat("Floor height", &floor.height, -10.0f, 0.0f, "%.1f");
	drawMaterialSection(floor.material, "Floor material");
}

} // namespace

void drawSceneWindow(scene::Scene& scene) {
	ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(420.0f, 660.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Lab 2: camera and lighting");
	ImGui::PushItemWidth(200.0f);

	ImGui::Text("%.0f FPS", ImGui::GetIO().Framerate);
	drawCameraSection(scene.camera);
	drawLightingSection(scene);
	drawPointLightsSection(scene);
	drawFloorSection(scene.floor);
	drawObjectsSection(scene);

	// Разделы ниже меняют только выбранный объект.
	char title[32];
	std::snprintf(title, sizeof(title), "Object %d", scene.selected + 1);
	ImGui::SeparatorText(title);

	scene::Object& object = scene.objects[scene.selected];
	drawMaterialSection(object.material, "Material");
	drawTransformSection(object.transform);
	drawAnimationSection(object.animation);

	ImGui::PopItemWidth();
	ImGui::End();
}

} // namespace ui
