#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace geometry {

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint16_t> indices;
};

// Правильный додекаэдр, вписанный в единичную сферу: 12 пятиугольных граней, каждая разбита на 3 треугольника.
// У каждой грани свои 5 вершин (всего 60), потому что нормаль у граней разная: освещение получается плоским, с чёткими рёбрами.
// Нормаль грани считается как векторное произведение рёбер её первого треугольника.
// Треугольники обходятся против часовой стрелки, если смотреть снаружи.
Mesh makeDodecahedron();

// Квадрат [-1, 1] x [-1, 1] в плоскости XZ с нормалью +Y, обход против часовой стрелки при взгляде сверху.
Mesh makePlane();

} // namespace geometry
