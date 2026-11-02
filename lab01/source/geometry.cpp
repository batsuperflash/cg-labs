#include "geometry.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "color.hpp"

namespace geometry {

Mesh makeDodecahedron() {
	constexpr float phi = std::numbers::phi_v<float>;
	constexpr float inv_phi = 1.0f / phi;
	constexpr float signs[] = { -1.0f, 1.0f };

	// Вершины: (±1, ±1, ±1), (0, ±1/φ, ±φ), (±1/φ, ±φ, 0), (±φ, 0, ±1/φ). Все лежат на сфере радиуса √3.
	std::vector<glm::vec3> positions;
	for (float x : signs) {
		for (float y : signs) {
			for (float z : signs) {
				positions.emplace_back(x, y, z);
			}
		}
	}
	for (float a : signs) {
		for (float b : signs) {
			positions.emplace_back(0.0f, a * inv_phi, b * phi);
			positions.emplace_back(a * inv_phi, b * phi, 0.0f);
			positions.emplace_back(a * phi, 0.0f, b * inv_phi);
		}
	}

	// Нормали граней направлены на вершины двойственного икосаэдра: (0, ±φ, ±1) и циклические перестановки.
	std::vector<glm::vec3> normals;
	for (float a : signs) {
		for (float b : signs) {
			normals.push_back(glm::normalize(glm::vec3(0.0f, a * phi, b)));
			normals.push_back(glm::normalize(glm::vec3(a, 0.0f, b * phi)));
			normals.push_back(glm::normalize(glm::vec3(a * phi, b, 0.0f)));
		}
	}

	Mesh mesh;
	for (const glm::vec3& p : positions) {
		const glm::vec3 position = p / std::sqrt(3.0f);
		mesh.vertices.push_back({ position, color::srgbToLinear(position * 0.5f + 0.5f) });
	}

	for (const glm::vec3& normal : normals) {
		// Грань — это 5 вершин, дальше всех продвинутых вдоль её нормали.
		float max_distance = -1.0f;
		for (const Vertex& vertex : mesh.vertices) {
			max_distance = std::max(max_distance, glm::dot(vertex.position, normal));
		}

		std::vector<uint16_t> face;
		glm::vec3 center(0.0f);
		for (size_t i = 0; i < mesh.vertices.size(); ++i) {
			if (max_distance - glm::dot(mesh.vertices[i].position, normal) < 1e-4f) {
				face.push_back(static_cast<uint16_t>(i));
				center += mesh.vertices[i].position;
			}
		}
		center /= static_cast<float>(face.size());

		// Сортировка по углу в базисе (u, w), где w = n × u, даёт обход против часовой стрелки при взгляде снаружи.
		const glm::vec3 u = glm::normalize(mesh.vertices[face[0]].position - center);
		const glm::vec3 w = glm::cross(normal, u);
		auto angle = [&](uint16_t index) {
			const glm::vec3 d = mesh.vertices[index].position - center;
			return std::atan2(glm::dot(d, w), glm::dot(d, u));
		};
		std::sort(face.begin(), face.end(), [&](uint16_t a, uint16_t b) {
			return angle(a) < angle(b);
		});

		for (size_t i = 1; i + 1 < face.size(); ++i) {
			mesh.indices.insert(mesh.indices.end(), { face[0], face[i], face[i + 1] });
		}
	}

	return mesh;
}

} // namespace geometry
