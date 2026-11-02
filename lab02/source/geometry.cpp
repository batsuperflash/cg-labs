#include "geometry.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

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
	for (glm::vec3& position : positions) {
		position /= std::sqrt(3.0f);
	}

	// Направления на центры граней — вершины двойственного икосаэдра: (0, ±φ, ±1) и циклические перестановки.
	// Они нужны только для того, чтобы найти вершины каждой грани.
	std::vector<glm::vec3> directions;
	for (float a : signs) {
		for (float b : signs) {
			directions.push_back(glm::normalize(glm::vec3(0.0f, a * phi, b)));
			directions.push_back(glm::normalize(glm::vec3(a, 0.0f, b * phi)));
			directions.push_back(glm::normalize(glm::vec3(a * phi, b, 0.0f)));
		}
	}

	Mesh mesh;
	for (const glm::vec3& direction : directions) {
		// Грань — это 5 вершин, дальше всех продвинутых вдоль её направления.
		float max_distance = -1.0f;
		for (const glm::vec3& position : positions) {
			max_distance = std::max(max_distance, glm::dot(position, direction));
		}

		std::vector<glm::vec3> face;
		glm::vec3 center(0.0f);
		for (const glm::vec3& position : positions) {
			if (max_distance - glm::dot(position, direction) < 1e-4f) {
				face.push_back(position);
				center += position;
			}
		}
		center /= static_cast<float>(face.size());

		// Сортировка по углу в базисе (u, w), где w = n × u, даёт обход против часовой стрелки при взгляде снаружи.
		const glm::vec3 u = glm::normalize(face[0] - center);
		const glm::vec3 w = glm::cross(direction, u);
		auto angle = [&](const glm::vec3& position) {
			const glm::vec3 d = position - center;
			return std::atan2(glm::dot(d, w), glm::dot(d, u));
		};
		std::sort(face.begin(), face.end(), [&](const glm::vec3& a, const glm::vec3& b) {
			return angle(a) < angle(b);
		});

		// Нормаль грани: перпендикуляр к двум рёбрам первого треугольника. При обходе против часовой стрелки
		// он смотрит наружу.
		const glm::vec3 normal = glm::normalize(glm::cross(face[1] - face[0], face[2] - face[0]));

		const uint16_t first = static_cast<uint16_t>(mesh.vertices.size());
		for (const glm::vec3& position : face) {
			mesh.vertices.push_back({ position, normal });
		}

		for (uint16_t i = 1; i + 1 < face.size(); ++i) {
			mesh.indices.insert(mesh.indices.end(), { first, uint16_t(first + i), uint16_t(first + i + 1) });
		}
	}

	return mesh;
}

Mesh makePlane() {
	const glm::vec3 normal(0.0f, 1.0f, 0.0f);

	Mesh mesh;
	mesh.vertices = {
		{ { -1.0f, 0.0f, 1.0f }, normal },
		{ { 1.0f, 0.0f, 1.0f }, normal },
		{ { 1.0f, 0.0f, -1.0f }, normal },
		{ { -1.0f, 0.0f, -1.0f }, normal },
	};
	mesh.indices = { 0, 1, 2, 0, 2, 3 };
	return mesh;
}

} // namespace geometry
