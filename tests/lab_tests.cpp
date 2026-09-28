// Проверки без окна и без Vulkan: матрицы сравниваются с GLM, геометрия — с известными свойствами додекаэдра.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "geometry.hpp"
#include "transform.hpp"

namespace {

int checks = 0;
int failures = 0;

void check(bool condition, const char* description) {
	++checks;
	if (!condition) {
		++failures;
		std::printf("FAIL: %s\n", description);
	}
}

bool approxEqual(const glm::mat4& a, const glm::mat4& b, float epsilon = 1e-5f) {
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 4; ++row) {
			if (std::abs(a[column][row] - b[column][row]) > epsilon) {
				return false;
			}
		}
	}
	return true;
}

// GLM строит проекции под OpenGL, где ось Y в clip space направлена вверх.
const glm::mat4 flip_y = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f));

void testAffine() {
	const glm::mat4 identity(1.0f);
	const glm::vec3 v(1.5f, -2.0f, 0.25f);

	check(approxEqual(transform::translate(v), glm::translate(identity, v)), "translate matches GLM");
	check(approxEqual(transform::scale(v), glm::scale(identity, v)), "scale matches GLM");

	for (float angle : { 0.3f, -1.2f, 2.5f }) {
		check(approxEqual(transform::rotateX(angle), glm::rotate(identity, angle, glm::vec3(1.0f, 0.0f, 0.0f))),
		      "rotateX matches GLM");
		check(approxEqual(transform::rotateY(angle), glm::rotate(identity, angle, glm::vec3(0.0f, 1.0f, 0.0f))),
		      "rotateY matches GLM");
		check(approxEqual(transform::rotateZ(angle), glm::rotate(identity, angle, glm::vec3(0.0f, 0.0f, 1.0f))),
		      "rotateZ matches GLM");
	}

	const glm::vec3 eye(1.0f, 2.0f, 5.0f);
	const glm::vec3 target(0.0f, 0.5f, -1.0f);
	const glm::vec3 up(0.0f, 1.0f, 0.0f);
	check(approxEqual(transform::lookAt(eye, target, up), glm::lookAtRH(eye, target, up)), "lookAt matches GLM");
}

void testProjections() {
	const float fov = glm::radians(60.0f);
	const float aspect = 16.0f / 9.0f;
	const float z_near = 0.1f;
	const float z_far = 100.0f;

	const glm::mat4 perspective = transform::perspective(fov, aspect, z_near, z_far);
	check(approxEqual(perspective, flip_y * glm::perspectiveRH_ZO(fov, aspect, z_near, z_far)),
	      "perspective matches GLM with flipped Y");

	const glm::mat4 orthographic = transform::orthographic(-2.0f, 3.0f, -1.5f, 2.5f, z_near, z_far);
	check(approxEqual(orthographic, flip_y * glm::orthoRH_ZO(-2.0f, 3.0f, -1.5f, 2.5f, z_near, z_far)),
	      "orthographic matches GLM with flipped Y");

	for (const glm::mat4& projection : { perspective, orthographic }) {
		const glm::vec4 at_near = projection * glm::vec4(0.0f, 0.0f, -z_near, 1.0f);
		const glm::vec4 at_far = projection * glm::vec4(0.0f, 0.0f, -z_far, 1.0f);
		check(std::abs(at_near.z / at_near.w) < 1e-5f, "near plane maps to depth 0");
		check(std::abs(at_far.z / at_far.w - 1.0f) < 1e-4f, "far plane maps to depth 1");

		const glm::vec4 above = projection * glm::vec4(0.0f, 1.0f, -1.0f, 1.0f);
		check(above.y / above.w < 0.0f, "positive Y goes to the upper half of the screen");
	}
}

void testDodecahedron() {
	const geometry::Mesh mesh = geometry::makeDodecahedron();
	check(mesh.vertices.size() == 20, "20 vertices");
	check(mesh.indices.size() == 108, "36 triangles");
	if (mesh.vertices.size() != 20 || mesh.indices.size() % 3 != 0) {
		return;
	}

	auto position = [&](uint16_t index) { return mesh.vertices[index].position; };

	for (const geometry::Vertex& vertex : mesh.vertices) {
		check(std::abs(glm::length(vertex.position) - 1.0f) < 1e-5f, "vertex lies on the unit sphere");
	}

	std::map<std::pair<uint16_t, uint16_t>, int> directed_edges;
	for (size_t i = 0; i < mesh.indices.size(); i += 3) {
		const uint16_t triangle[] = { mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2] };
		for (int k = 0; k < 3; ++k) {
			++directed_edges[{ triangle[k], triangle[(k + 1) % 3] }];
		}

		const glm::vec3 a = position(triangle[0]);
		const glm::vec3 b = position(triangle[1]);
		const glm::vec3 c = position(triangle[2]);
		check(glm::dot(glm::cross(b - a, c - a), a + b + c) > 0.0f, "triangle is counter-clockwise from outside");
	}

	// Каждое ребро треугольника встречается ровно дважды и в противоположных направлениях:
	// поверхность замкнута, а все треугольники обходятся одинаково.
	bool closed = true;
	for (const auto& [edge, count] : directed_edges) {
		closed = closed && count == 1 && directed_edges.count({ edge.second, edge.first }) == 1;
	}
	check(closed, "surface is closed and consistently oriented");

	// Рёбра многогранника — самые короткие рёбра треугольников, остальные — диагонали пятиугольников (в φ раз длиннее).
	float edge_length = 10.0f;
	for (const auto& [edge, count] : directed_edges) {
		edge_length = std::min(edge_length, glm::length(position(edge.first) - position(edge.second)));
	}

	int edges = 0;
	std::array<int, 20> degree{};
	for (const auto& [edge, count] : directed_edges) {
		if (edge.first > edge.second) {
			continue;
		}
		const float length = glm::length(position(edge.first) - position(edge.second));
		if (std::abs(length - edge_length) < 1e-4f) {
			++edges;
			++degree[edge.first];
			++degree[edge.second];
		} else {
			check(std::abs(length - edge_length * std::numbers::phi_v<float>) < 1e-4f,
			      "other triangle edges are pentagon diagonals");
		}
	}

	const int vertices = static_cast<int>(mesh.vertices.size());
	const int faces = static_cast<int>(mesh.indices.size() / 9);
	check(edges == 30, "30 edges of equal length");
	check(faces == 12, "12 pentagonal faces");
	check(vertices - edges + faces == 2, "Euler characteristic V - E + F = 2");
	check(std::all_of(degree.begin(), degree.end(), [](int d) { return d == 3; }), "every vertex belongs to 3 faces");
}

} // namespace

int main() {
	testAffine();
	testProjections();
	testDodecahedron();

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
