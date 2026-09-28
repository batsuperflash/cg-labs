#pragma once

#include "scene.hpp"

namespace ui {

// Окно с настройками сцены. Меняет сцену напрямую; вызывается между ImGui::NewFrame() и ImGui::Render().
void drawSceneWindow(scene::Scene& scene);

} // namespace ui
