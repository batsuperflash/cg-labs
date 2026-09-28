// Проверки без окна и без Vulkan: матрицы сравниваются с GLM, геометрия — с известными свойствами додекаэдра,
// логика сцены — с формулами из задания.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "color.hpp"
#include "geometry.hpp"
#include "scene.hpp"
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

bool approxEqual(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-5f) {
	return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(epsilon)));
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

		const glm::vec3 expected_color = color::srgbToLinear(vertex.position * 0.5f + 0.5f);
		check(approxEqual(vertex.color, expected_color) &&
		      glm::all(glm::greaterThanEqual(vertex.color, glm::vec3(0.0f))) &&
		      glm::all(glm::lessThanEqual(vertex.color, glm::vec3(1.0f))),
		      "vertex color is position * 0.5 + 0.5 converted from sRGB");
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

void testColor() {
	check(color::srgbToLinear(0.0f) == 0.0f, "sRGB 0 stays 0");
	check(std::abs(color::srgbToLinear(1.0f) - 1.0f) < 1e-6f, "sRGB 1 stays 1");
	check(std::abs(color::srgbToLinear(0.5f) - 0.2140f) < 1e-4f, "sRGB 0.5 is 0.214 in linear space");
}

void testTrajectoryAndAnimation() {
	const scene::Trajectory trajectory;
	const glm::vec3 start(trajectory.radius * std::sin(trajectory.phase_shift), 0.0f, 0.0f);
	check(approxEqual(scene::trajectoryPoint(trajectory, 0.0f), start), "trajectory starts at (R sin d, 0, 0)");

	for (float phase : { 0.3f, 1.7f, 4.0f }) {
		const glm::vec3 point = scene::trajectoryPoint(trajectory, phase);
		const glm::vec3 next_period = scene::trajectoryPoint(trajectory, phase + 2.0f * std::numbers::pi_v<float>);
		check(approxEqual(point, next_period, 1e-4f), "trajectory repeats after 2 pi");
	}

	scene::Animation animation;
	animation.playing = false;
	scene::advance(animation, 0.5f);
	check(animation.time == 0.0f && animation.spin_angle == 0.0f, "paused animation does not move");

	animation.playing = true;
	scene::advance(animation, 0.5f);
	check(std::abs(animation.time - 0.5f * animation.speed) < 1e-6f, "animation time grows by dt * speed");
	check(std::abs(animation.spin_angle - 0.5f * glm::radians(animation.spin_speed_degrees)) < 1e-6f,
	      "spin angle grows by dt * spin speed");
}

void testObjectMatrices() {
	const glm::mat4 identity(1.0f);

	scene::Object object;
	object.animation.trajectory.radius = 0.0f;
	object.transform.position = glm::vec3(1.0f, -2.0f, 0.5f);
	object.transform.rotation_degrees = glm::vec3(30.0f, -45.0f, 60.0f);
	object.transform.scale = glm::vec3(2.0f, 0.5f, 1.5f);

	const glm::vec3 angles = glm::radians(object.transform.rotation_degrees);
	glm::mat4 expected = glm::translate(identity, object.transform.position);
	expected = glm::rotate(expected, angles.z, glm::vec3(0.0f, 0.0f, 1.0f));
	expected = glm::rotate(expected, angles.y, glm::vec3(0.0f, 1.0f, 0.0f));
	expected = glm::rotate(expected, angles.x, glm::vec3(1.0f, 0.0f, 0.0f));
	expected = glm::scale(expected, object.transform.scale);
	check(approxEqual(scene::modelMatrix(object), expected), "model matrix is T * Rz * Ry * Rx * S");

	scene::Object stretched;
	stretched.animation.trajectory.radius = 0.0f;
	stretched.transform.position = glm::vec3(0.0f, 0.0f, 1.0f);
	stretched.transform.rotation_degrees = glm::vec3(0.0f, 0.0f, 90.0f);
	stretched.transform.scale = glm::vec3(2.0f, 1.0f, 1.0f);
	const glm::vec4 moved = scene::modelMatrix(stretched) * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
	check(approxEqual(glm::vec3(moved), glm::vec3(0.0f, 2.0f, 1.0f)), "scale is applied before rotation");

	// Верхний край кадра в плоскости z = 0 одинаков в обеих проекциях: при переключении размер не скачет.
	scene::Camera camera;
	const float half_height = camera.distance * std::tan(glm::radians(camera.fov_degrees) / 2.0f);
	for (scene::Projection projection : { scene::Projection::Perspective, scene::Projection::Orthographic }) {
		camera.projection = projection;
		const glm::vec4 clip = scene::projectionMatrix(camera, 16.0f / 9.0f) * scene::viewMatrix(camera) *
		                       glm::vec4(0.0f, half_height, 0.0f, 1.0f);
		check(std::abs(clip.y / clip.w + 1.0f) < 1e-5f, "top of the view at z = 0 matches in both projections");
	}
}

void testObjectList() {
	scene::Scene state;
	for (int i = 0; i < 10; ++i) {
		scene::addObject(state);
	}
	check(state.object_count == scene::max_objects, "no more than 8 objects");
	check(state.selected == scene::max_objects - 1, "added object becomes selected");

	bool distinct_offsets = true;
	for (int i = 0; i < state.object_count; ++i) {
		for (int j = i + 1; j < state.object_count; ++j) {
			distinct_offsets = distinct_offsets && state.objects[i].animation.offset != state.objects[j].animation.offset;
		}
	}
	check(distinct_offsets, "objects start at different points of the trajectory");

	state.selected = 2;
	const float next_offset = state.objects[3].animation.offset;
	scene::removeSelected(state);
	check(state.object_count == scene::max_objects - 1, "removal decreases the object count");
	check(state.objects[2].animation.offset == next_offset, "removal shifts the following objects");

	for (int i = 0; i < 10; ++i) {
		state.selected = state.object_count - 1;
		scene::removeSelected(state);
	}
	check(state.object_count == 1 && state.selected == 0, "at least one object remains");
}

} // namespace

int main() {
	testAffine();
	testProjections();
	testDodecahedron();
	testColor();
	testTrajectoryAndAnimation();
	testObjectMatrices();
	testObjectList();

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
