// Проверки без окна и без Vulkan: матрицы сравниваются с GLM, геометрия — с известными свойствами додекаэдра,
// логика сцены — с формулами из задания.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
	check(mesh.vertices.size() == 60, "12 faces with 5 own vertices each");
	check(mesh.indices.size() == 108, "36 triangles");
	if (mesh.vertices.size() != 60 || mesh.indices.size() % 3 != 0) {
		return;
	}

	for (const geometry::Vertex& vertex : mesh.vertices) {
		check(std::abs(glm::length(vertex.position) - 1.0f) < 1e-5f, "vertex lies on the unit sphere");
		check(std::abs(glm::length(vertex.normal) - 1.0f) < 1e-5f, "normal has unit length");
		check(glm::dot(vertex.normal, vertex.position) > 0.5f, "normal points outwards");
	}

	// Нормаль каждой вершины перпендикулярна обеим сторонам её треугольника и совпадает с нормалью треугольника.
	std::vector<glm::vec3> distinct_normals;
	for (size_t i = 0; i < mesh.indices.size(); i += 3) {
		const geometry::Vertex& a = mesh.vertices[mesh.indices[i]];
		const geometry::Vertex& b = mesh.vertices[mesh.indices[i + 1]];
		const geometry::Vertex& c = mesh.vertices[mesh.indices[i + 2]];

		const glm::vec3 triangle_normal = glm::normalize(glm::cross(b.position - a.position, c.position - a.position));
		check(approxEqual(a.normal, triangle_normal, 1e-4f) && approxEqual(b.normal, triangle_normal, 1e-4f) &&
		      approxEqual(c.normal, triangle_normal, 1e-4f),
		      "vertex normals equal the normal of the triangle (counter-clockwise from outside)");
		check(glm::dot(triangle_normal, a.position + b.position + c.position) > 0.0f,
		      "triangle is counter-clockwise from outside");

		const bool known = std::any_of(distinct_normals.begin(), distinct_normals.end(), [&](const glm::vec3& n) {
			return approxEqual(n, triangle_normal, 1e-4f);
		});
		if (!known) {
			distinct_normals.push_back(triangle_normal);
		}
	}
	check(distinct_normals.size() == 12, "12 different face normals");

	// Вершины с одинаковым положением (по одной от каждой из трёх граней) склеиваются:
	// дальше проверяется форма самого многогранника, как при общих вершинах.
	std::vector<glm::vec3> welded;
	std::vector<int> id(mesh.vertices.size());
	for (size_t i = 0; i < mesh.vertices.size(); ++i) {
		const auto found = std::find_if(welded.begin(), welded.end(), [&](const glm::vec3& p) {
			return approxEqual(p, mesh.vertices[i].position, 1e-4f);
		});
		id[i] = int(found - welded.begin());
		if (found == welded.end()) {
			welded.push_back(mesh.vertices[i].position);
		}
	}
	check(welded.size() == 20, "20 distinct vertex positions");
	if (welded.size() != 20) {
		return;
	}

	std::map<std::pair<int, int>, int> directed_edges;
	for (size_t i = 0; i < mesh.indices.size(); i += 3) {
		const int triangle[] = { id[mesh.indices[i]], id[mesh.indices[i + 1]], id[mesh.indices[i + 2]] };
		for (int k = 0; k < 3; ++k) {
			++directed_edges[{ triangle[k], triangle[(k + 1) % 3] }];
		}
	}

	// Каждое ребро треугольника встречается ровно один раз в каждом направлении:
	// поверхность замкнута, а все треугольники обходятся одинаково.
	bool closed = true;
	for (const auto& [edge, count] : directed_edges) {
		closed = closed && count == 1 && directed_edges.count({ edge.second, edge.first }) == 1;
	}
	check(closed, "surface is closed and consistently oriented");

	// Рёбра многогранника — самые короткие рёбра треугольников, остальные — диагонали пятиугольников (в φ раз длиннее).
	float edge_length = 10.0f;
	for (const auto& [edge, count] : directed_edges) {
		edge_length = std::min(edge_length, glm::length(welded[edge.first] - welded[edge.second]));
	}

	int edges = 0;
	std::array<int, 20> degree{};
	for (const auto& [edge, count] : directed_edges) {
		if (edge.first > edge.second) {
			continue;
		}
		const float length = glm::length(welded[edge.first] - welded[edge.second]);
		if (std::abs(length - edge_length) < 1e-4f) {
			++edges;
			++degree[edge.first];
			++degree[edge.second];
		} else {
			check(std::abs(length - edge_length * std::numbers::phi_v<float>) < 1e-4f,
			      "other triangle edges are pentagon diagonals");
		}
	}

	const int vertices = static_cast<int>(welded.size());
	const int faces = static_cast<int>(mesh.indices.size() / 9);
	check(edges == 30, "30 edges of equal length");
	check(faces == 12, "12 pentagonal faces");
	check(vertices - edges + faces == 2, "Euler characteristic V - E + F = 2");
	check(std::all_of(degree.begin(), degree.end(), [](int d) { return d == 3; }), "every vertex belongs to 3 faces");
}

void testPlane() {
	const geometry::Mesh mesh = geometry::makePlane();
	check(mesh.vertices.size() == 4 && mesh.indices.size() == 6, "plane is two triangles");

	for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
		const glm::vec3 a = mesh.vertices[mesh.indices[i]].position;
		const glm::vec3 b = mesh.vertices[mesh.indices[i + 1]].position;
		const glm::vec3 c = mesh.vertices[mesh.indices[i + 2]].position;
		check(approxEqual(glm::normalize(glm::cross(b - a, c - a)), glm::vec3(0.0f, 1.0f, 0.0f)),
		      "plane triangles are counter-clockwise from above");
	}
	for (const geometry::Vertex& vertex : mesh.vertices) {
		check(approxEqual(vertex.normal, glm::vec3(0.0f, 1.0f, 0.0f)), "plane normal points up");
	}
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
	camera.position = glm::vec3(0.0f, 0.0f, camera.focus_distance);
	camera.yaw_degrees = 0.0f;
	camera.pitch_degrees = 0.0f;
	const float half_height = camera.focus_distance * std::tan(glm::radians(camera.fov_degrees) / 2.0f);
	for (scene::Projection projection : { scene::Projection::Perspective, scene::Projection::Orthographic }) {
		camera.projection = projection;
		const glm::vec4 clip = scene::projectionMatrix(camera, 16.0f / 9.0f) * scene::viewMatrix(camera) *
		                       glm::vec4(0.0f, half_height, 0.0f, 1.0f);
		check(std::abs(clip.y / clip.w + 1.0f) < 1e-5f, "top of the view at the focus distance matches in both projections");
	}
}

void testNormalMatrix() {
	scene::Object object;
	object.animation.trajectory.radius = 0.0f;
	object.animation.spin_angle = 0.7f;
	object.transform.position = glm::vec3(1.0f, -2.0f, 0.5f);
	object.transform.rotation_degrees = glm::vec3(30.0f, -45.0f, 60.0f);
	object.transform.scale = glm::vec3(2.0f, 0.5f, 1.5f);

	const glm::mat3 expected = glm::inverseTranspose(glm::mat3(scene::modelMatrix(object)));
	const glm::mat3 actual = glm::mat3(scene::normalMatrix(object));
	check(approxEqual(glm::mat4(actual), glm::mat4(expected), 1e-4f), "normal matrix is the inverse transpose of the model matrix");

	// При растяжении нормаль остаётся перпендикулярной поверхности, а у обычной матрицы модели — нет.
	const glm::vec3 edge = glm::vec3(1.0f, -1.0f, 0.0f);
	const glm::vec3 normal = glm::vec3(1.0f, 1.0f, 0.0f);
	const glm::mat4 model = scene::modelMatrix(object);
	const glm::vec3 moved_edge = glm::mat3(model) * edge;
	check(std::abs(glm::dot(glm::normalize(actual * normal), glm::normalize(moved_edge))) < 1e-5f,
	      "transformed normal stays perpendicular to the transformed surface");
}

void testCamera() {
	check(approxEqual(scene::directionFromAngles(0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)), "zero angles look along -Z");
	check(approxEqual(scene::directionFromAngles(90.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)), "yaw 90 looks along +X");
	check(approxEqual(scene::directionFromAngles(0.0f, 90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), "pitch 90 looks up");
	check(std::abs(glm::length(scene::directionFromAngles(33.0f, -61.0f)) - 1.0f) < 1e-5f, "direction has unit length");

	scene::Camera camera;
	camera.position = glm::vec3(1.0f, 2.0f, 3.0f);
	camera.yaw_degrees = 25.0f;
	camera.pitch_degrees = -15.0f;
	const glm::vec3 forward = scene::cameraForward(camera);
	check(approxEqual(scene::viewMatrix(camera),
	                  glm::lookAtRH(camera.position, camera.position + forward, glm::vec3(0.0f, 1.0f, 0.0f))),
	      "view matrix matches GLM lookAt from position and angles");

	// Точка прямо перед камерой оказывается на оси -Z в системе камеры.
	const glm::vec4 in_front = scene::viewMatrix(camera) * glm::vec4(camera.position + 4.0f * forward, 1.0f);
	check(approxEqual(glm::vec3(in_front), glm::vec3(0.0f, 0.0f, -4.0f), 1e-4f), "point ahead of the camera is on the -Z axis");

	camera = {};
	camera.yaw_degrees = 0.0f;
	camera.pitch_degrees = 0.0f;
	camera.position = glm::vec3(0.0f);
	camera.move_speed = 2.0f;
	scene::moveCamera(camera, glm::vec3(0.0f, 0.0f, 1.0f), 0.5f);
	check(approxEqual(camera.position, glm::vec3(0.0f, 0.0f, -1.0f)), "W moves forward by speed * dt");
	scene::moveCamera(camera, glm::vec3(1.0f, 0.0f, 0.0f), 0.5f);
	check(approxEqual(camera.position, glm::vec3(1.0f, 0.0f, -1.0f)), "D moves to the right");
	scene::moveCamera(camera, glm::vec3(0.0f, 1.0f, 0.0f), 0.5f);
	check(approxEqual(camera.position, glm::vec3(1.0f, 1.0f, -1.0f)), "E moves up");

	scene::rotateCamera(camera, 400.0f, 500.0f);
	check(camera.pitch_degrees == 89.0f, "pitch is clamped before the camera flips over");
	check(camera.yaw_degrees >= -180.0f && camera.yaw_degrees <= 180.0f, "yaw wraps into [-180, 180]");
}

void testPointLightList() {
	scene::Scene state;
	check(state.point_lights.empty(), "a scene starts without point lights");

	for (int i = 0; i < 20; ++i) {
		scene::addPointLight(state);
	}
	check(int(state.point_lights.size()) == scene::max_point_lights, "no more than 16 point lights");
	check(state.selected_point == scene::max_point_lights - 1, "added light becomes selected");

	state.selected_point = 1;
	const glm::vec3 next_color = state.point_lights[2].color;
	const int before = int(state.point_lights.size());
	scene::removeSelectedPointLight(state);
	check(int(state.point_lights.size()) == before - 1, "removal decreases the light count");
	check(state.point_lights[1].color == next_color, "removal shifts the following lights");

	while (!state.point_lights.empty()) {
		state.selected_point = int(state.point_lights.size()) - 1;
		scene::removeSelectedPointLight(state);
	}
	scene::removeSelectedPointLight(state);
	check(state.point_lights.empty() && state.selected_point == 0, "all point lights can be removed");
}

void testDefaultScene() {
	const scene::Scene state = scene::makeDefaultScene();
	check(state.object_count >= 1 && state.object_count <= scene::max_objects, "default scene has objects");
	check(state.sun.enabled && state.sun.intensity > 0.0f, "default scene is lit by the directional light");
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
	testPlane();
	testNormalMatrix();
	testCamera();
	testPointLightList();
	testDefaultScene();
	testTrajectoryAndAnimation();
	testObjectMatrices();
	testObjectList();

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
