#include "ui.hpp"

#include <algorithm>
#include <cstdio>
#include <span>
#include <vector>

#include <imgui.h>

namespace ui {

namespace {

void drawObjectsSection(scene::Scene& scene, std::span<const MaterialEntry> materials) {
	if (!ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	// Выпадающий список занимает одну строку, поэтому окно не растёт с числом объектов. Рядом с номером написано название текстуры.
	auto describe = [&](int i, char* text, size_t size) {
		const int texture = std::clamp(scene.objects[i].material.texture, 0, std::max(int(materials.size()) - 1, 0));
		std::snprintf(text, size, "Object %d: %s", i + 1, materials.empty() ? "-" : materials[texture].name.c_str());
	};

	char preview[64];
	describe(scene.selected, preview, sizeof(preview));
	if (ImGui::BeginCombo("Selected", preview)) {
		for (int i = 0; i < scene.object_count; ++i) {
			char label[64];
			describe(i, label, sizeof(label));

			ImGui::PushID(i);
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

// Выпадающий список материалов: название выбранного в строке, остальные в списке.
void drawTextureCombo(const char* label, int& index, std::span<const MaterialEntry> materials) {
	if (materials.empty()) {
		return;
	}

	index = std::clamp(index, 0, int(materials.size()) - 1);
	if (ImGui::BeginCombo(label, materials[index].name.c_str())) {
		for (int i = 0; i < int(materials.size()); ++i) {
			ImGui::PushID(i);
			if (ImGui::Selectable(materials[i].name.c_str(), index == i)) {
				index = i;
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
}

void drawMaterialSection(scene::Material& material, const char* title, std::span<const MaterialEntry> materials) {
	if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	drawTextureCombo("Texture", material.texture, materials);
	ImGui::ColorEdit3("Tint", &material.albedo.x);
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

// Список источников одного вида: выбор в выпадающем списке, Add и Remove. Возвращает выбранный источник или nullptr.
template <typename Light>
Light* drawLightList(const char* name, std::vector<Light>& lights, int& selected, int max_count, void (*add)(scene::Scene&),
                     void (*remove)(scene::Scene&), scene::Scene& scene) {
	auto drawSwatch = [](const glm::vec3& color) {
		const float size = ImGui::GetFrameHeight();
		ImGui::ColorButton("##swatch", ImVec4(color.r, color.g, color.b, 1.0f), ImGuiColorEditFlags_NoTooltip,
		                   ImVec2(size, size));
		ImGui::SameLine();
	};

	const int count = int(lights.size());

	if (count > 0) {
		char preview[48];
		std::snprintf(preview, sizeof(preview), "%s %d", name, selected + 1);
		drawSwatch(lights[selected].color);
		if (ImGui::BeginCombo("Selected", preview)) {
			for (int i = 0; i < count; ++i) {
				char label[48];
				std::snprintf(label, sizeof(label), "%s %d", name, i + 1);

				ImGui::PushID(i);
				drawSwatch(lights[i].color);
				if (ImGui::Selectable(label, selected == i)) {
					selected = i;
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
	}

	ImGui::BeginDisabled(count >= max_count);
	if (ImGui::Button("Add")) {
		add(scene);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(count == 0);
	if (ImGui::Button("Remove")) {
		remove(scene);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::TextDisabled("%d of %d", count, max_count);

	return lights.empty() ? nullptr : &lights[selected];
}

void drawPointLightsSection(scene::Scene& scene) {
	if (!ImGui::CollapsingHeader("Point lights", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::PushID("point");
	scene::PointLight* light = drawLightList("Point light", scene.point_lights, scene.selected_point,
	                                         scene::max_point_lights, scene::addPointLight,
	                                         scene::removeSelectedPointLight, scene);
	if (light != nullptr) {
		ImGui::DragFloat3("Position", &light->position.x, 0.05f, -20.0f, 20.0f, "%.2f");
		ImGui::ColorEdit3("Color", &light->color.x);
		ImGui::SliderFloat("Intensity", &light->intensity, 0.0f, 200.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
		ImGui::TextDisabled("Brightness falls off as intensity / distance^2");
	}
	ImGui::PopID();
}

void drawSpotLightsSection(scene::Scene& scene) {
	if (!ImGui::CollapsingHeader("Spot lights", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::PushID("spot");
	scene::SpotLight* light = drawLightList("Spot light", scene.spot_lights, scene.selected_spot,
	                                        scene::max_spot_lights, scene::addSpotLight,
	                                        scene::removeSelectedSpotLight, scene);
	if (light != nullptr) {
		ImGui::DragFloat3("Position", &light->position.x, 0.05f, -20.0f, 20.0f, "%.2f");
		ImGui::SliderFloat("Yaw", &light->yaw_degrees, -180.0f, 180.0f, "%.0f deg");
		ImGui::SliderFloat("Pitch", &light->pitch_degrees, -89.0f, 89.0f, "%.0f deg");
		ImGui::ColorEdit3("Color", &light->color.x);
		ImGui::SliderFloat("Intensity", &light->intensity, 0.0f, 400.0f, "%.1f", ImGuiSliderFlags_Logarithmic);

		// Внутренний угол не может быть больше внешнего.
		ImGui::SliderFloat("Inner angle", &light->inner_degrees, 0.0f, 85.0f, "%.1f deg");
		ImGui::SliderFloat("Outer angle", &light->outer_degrees, 0.0f, 89.0f, "%.1f deg");
		light->inner_degrees = std::min(light->inner_degrees, light->outer_degrees);
		ImGui::TextDisabled("Fades out between the inner and outer angle");
	}
	ImGui::PopID();
}

void drawVisualizationSection(scene::Scene& scene) {
	if (!ImGui::CollapsingHeader("Light visualization", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::Checkbox("Show light markers", &scene.show_light_markers);
	ImGui::Checkbox("Show spot light cones", &scene.show_spot_cones);
	ImGui::TextDisabled("Markers are cubes in the color of the light, unlit");
}

void drawFloorSection(scene::Floor& floor, std::span<const MaterialEntry> materials) {
	if (!ImGui::CollapsingHeader("Floor")) {
		return;
	}

	ImGui::Checkbox("Show floor", &floor.visible);
	ImGui::SliderFloat("Floor height", &floor.height, -10.0f, 0.0f, "%.1f");
	ImGui::SliderFloat("Texture tiling", &floor.tiling, 1.0f, 20.0f, "%.1f");
	drawMaterialSection(floor.material, "Floor material", materials);
}

} // namespace

void drawSceneWindow(scene::Scene& scene, std::span<const MaterialEntry> materials) {
	ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(420.0f, 660.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Lab 3: texturing");
	ImGui::PushItemWidth(200.0f);

	ImGui::Text("%.0f FPS", ImGui::GetIO().Framerate);
	drawCameraSection(scene.camera);
	drawLightingSection(scene);
	drawPointLightsSection(scene);
	drawSpotLightsSection(scene);
	drawVisualizationSection(scene);
	drawFloorSection(scene.floor, materials);
	drawObjectsSection(scene, materials);

	// Разделы ниже меняют только выбранный объект.
	char title[32];
	std::snprintf(title, sizeof(title), "Object %d", scene.selected + 1);
	ImGui::SeparatorText(title);

	scene::Object& object = scene.objects[scene.selected];
	drawMaterialSection(object.material, "Material", materials);
	drawTransformSection(object.transform);
	drawAnimationSection(object.animation);

	ImGui::PopItemWidth();
	ImGui::End();
}

} // namespace ui
