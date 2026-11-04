#pragma once

#include <span>
#include <string>

#include "scene.hpp"

namespace ui {

// То, что интерфейсу нужно знать о материале, чтобы показать его в списке.
struct MaterialEntry {
	std::string name;
	bool has_specular = false;
	bool has_emissive = false;
};

// Окно с настройками сцены. Меняет сцену напрямую; вызывается между ImGui::NewFrame() и ImGui::Render().
// materials — список материалов (текстур), из которого выбираются текстуры объектов и пола.
void drawSceneWindow(scene::Scene& scene, std::span<const MaterialEntry> materials);

} // namespace ui
