#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace geometry {

struct Vertex {
	glm::vec3 position;
	glm::vec3 color;
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint16_t> indices;
};

// Правильный додекаэдр, вписанный в единичную сферу: 20 вершин, 12 пятиугольных граней,
// каждая разбита на 3 треугольника. Треугольники обходятся против часовой стрелки, если смотреть снаружи.
// Цвет вершины вычисляется из её положения: position * 0.5 + 0.5.
Mesh makeDodecahedron();

} // namespace geometry
