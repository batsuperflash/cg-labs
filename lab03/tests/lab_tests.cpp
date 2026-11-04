// Проверки без окна и без Vulkan: матрицы сравниваются с GLM, геометрия — с известными свойствами додекаэдра,
// логика сцены — с формулами из задания, загрузка изображений — с файлами из textures.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <numbers>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "geometry.hpp"
#include "image.hpp"
#include "materials.hpp"
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

void testCubeAndCone() {
	const geometry::Mesh cube = geometry::makeCube();
	check(cube.vertices.size() == 24 && cube.indices.size() == 36, "cube has 24 vertices and 12 triangles");
	for (size_t i = 0; i + 2 < cube.indices.size(); i += 3) {
		const geometry::Vertex& a = cube.vertices[cube.indices[i]];
		const geometry::Vertex& b = cube.vertices[cube.indices[i + 1]];
		const geometry::Vertex& c = cube.vertices[cube.indices[i + 2]];
		check(approxEqual(glm::normalize(glm::cross(b.position - a.position, c.position - a.position)), a.normal, 1e-5f),
		      "cube triangles are counter-clockwise from outside");
	}

	const geometry::Mesh cone = geometry::makeSpotCone();
	check(cone.indices.size() % 2 == 0, "cone is a list of lines");
	for (uint16_t index : cone.indices) {
		check(index < cone.vertices.size(), "cone line indices are valid");
	}
	check(cone.vertices.size() > 2 && approxEqual(cone.vertices[0].position, glm::vec3(0.0f)), "cone starts at the origin");
	for (size_t i = 1; i + 1 < cone.vertices.size(); ++i) {
		const glm::vec3 p = cone.vertices[i].position;
		check(std::abs(p.z + 1.0f) < 1e-5f && std::abs(glm::length(glm::vec2(p)) - 1.0f) < 1e-5f,
		      "cone base is a unit circle at z = -1");
	}
}

void testLightVisualization() {
	scene::SpotLight light;
	light.position = glm::vec3(1.0f, 5.0f, -2.0f);
	light.yaw_degrees = 40.0f;
	light.pitch_degrees = -35.0f;
	light.outer_degrees = 30.0f;

	const glm::mat4 cone = scene::spotConeMatrix(light);
	const glm::vec3 direction = scene::directionFromAngles(light.yaw_degrees, light.pitch_degrees);
	check(approxEqual(glm::vec3(cone * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)), light.position), "cone apex is at the light");
	check(approxEqual(glm::vec3(cone * glm::vec4(0.0f, 0.0f, -1.0f, 1.0f)),
	                  light.position + scene::spot_cone_length * direction, 1e-4f),
	      "cone axis points along the light direction");

	// Точка основания конуса видна из вершины под внешним углом к оси.
	const glm::vec3 rim = glm::vec3(cone * glm::vec4(1.0f, 0.0f, -1.0f, 1.0f)) - light.position;
	check(std::abs(glm::dot(glm::normalize(rim), direction) - std::cos(glm::radians(light.outer_degrees))) < 1e-4f,
	      "cone rim lies at the outer angle from the axis");

	const glm::mat4 marker = scene::lightMarkerMatrix(light.position);
	check(approxEqual(glm::vec3(marker * glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)),
	                  light.position + glm::vec3(scene::light_marker_size)), "marker is a small cube at the light");
}

// Удвоенная площадь треугольника в плоскости текстурных координат. Знак показывает направление обхода.
float uvArea(const geometry::Vertex& a, const geometry::Vertex& b, const geometry::Vertex& c) {
	const glm::vec2 ab = b.uv - a.uv;
	const glm::vec2 ac = c.uv - a.uv;
	return ab.x * ac.y - ab.y * ac.x;
}

void testTextureCoordinates() {
	// Додекаэдр: пятиугольник каждой грани вписан в круг радиуса 0,5 с центром (0,5; 0,5).
	const geometry::Mesh dodecahedron = geometry::makeDodecahedron();
	for (size_t face = 0; face + 5 <= dodecahedron.vertices.size(); face += 5) {
		glm::vec2 center(0.0f);
		for (size_t i = 0; i < 5; ++i) {
			const glm::vec2 uv = dodecahedron.vertices[face + i].uv;
			check(std::abs(glm::length(uv - glm::vec2(0.5f)) - 0.5f) < 1e-4f, "face vertex lies on the circle of radius 0.5 in uv");
			center += uv / 5.0f;
		}
		check(glm::length(center - glm::vec2(0.5f)) < 1e-4f, "face center maps to the center of the texture");

		// У правильного пятиугольника все стороны равны и в плоскости текстуры тоже.
		const float side = glm::length(dodecahedron.vertices[face].uv - dodecahedron.vertices[face + 1].uv);
		for (size_t i = 1; i < 5; ++i) {
			const float next = glm::length(dodecahedron.vertices[face + i].uv - dodecahedron.vertices[face + (i + 1) % 5].uv);
			check(std::abs(next - side) < 1e-4f, "pentagon sides are equal in uv");
		}
	}

	// Текстура не зеркальна: треугольник, обходимый против часовой стрелки снаружи, в координатах изображения
	// (v растёт вниз) обходится по часовой стрелке, то есть имеет отрицательную площадь.
	for (size_t i = 0; i < dodecahedron.indices.size(); i += 3) {
		check(uvArea(dodecahedron.vertices[dodecahedron.indices[i]], dodecahedron.vertices[dodecahedron.indices[i + 1]],
		             dodecahedron.vertices[dodecahedron.indices[i + 2]]) < 0.0f,
		      "dodecahedron texture is not mirrored");
	}

	// Пол: (x, z) -> ((x + 1) / 2, (z + 1) / 2).
	const geometry::Mesh plane = geometry::makePlane();
	for (const geometry::Vertex& vertex : plane.vertices) {
		check(std::abs(vertex.uv.x - (vertex.position.x + 1.0f) * 0.5f) < 1e-6f &&
		      std::abs(vertex.uv.y - (vertex.position.z + 1.0f) * 0.5f) < 1e-6f,
		      "plane uv is the position in the unit square");
	}

	// Куб: на каждой грани текстура целиком и тоже не зеркальна.
	const geometry::Mesh cube = geometry::makeCube();
	for (size_t face = 0; face + 4 <= cube.vertices.size(); face += 4) {
		check(cube.vertices[face].uv == glm::vec2(0.0f, 1.0f) && cube.vertices[face + 2].uv == glm::vec2(1.0f, 0.0f),
		      "cube face shows the whole texture");
	}
	for (size_t i = 0; i < cube.indices.size(); i += 3) {
		check(uvArea(cube.vertices[cube.indices[i]], cube.vertices[cube.indices[i + 1]], cube.vertices[cube.indices[i + 2]]) < 0.0f,
		      "cube texture is not mirrored");
	}
}

void testImages() {
	check(image::mipLevelCount(1, 1) == 1, "1x1 image has one mip level");
	check(image::mipLevelCount(512, 512) == 10, "512x512 image has 10 mip levels");
	check(image::mipLevelCount(4, 2) == 3, "mip chain goes down to 1x1 along the larger side");
	check(image::mipLevelCount(5, 3) == 3, "odd sizes are rounded down");

	const image::Image black = image::solid(0, 0, 0);
	check(black.width == 1 && black.height == 1 && black.pixels.size() == 4 && black.pixels[3] == 255,
	      "service texture is one opaque pixel");

	image::Image brick;
	check(image::load("textures/brick_albedo.png", brick), "brick texture loads (run from the project root)");
	if (brick.pixels.empty()) {
		return;
	}
	check(brick.width == 512 && brick.height == 512 && brick.pixels.size() == 512u * 512u * 4u, "brick texture is 512x512 RGBA");
	check(brick.pixels[3] == 255, "brick texture is opaque");

	image::Image missing;
	check(!image::load("textures/does_not_exist.png", missing), "loading a missing file fails");
}

void testMaterials() {
	// Материалы ищутся по суффиксам _albedo, _specular и _emissive; порядок файлов в папке на результат не влияет.
	const std::filesystem::path directory = std::filesystem::temp_directory_path() / "lab03_materials_test";
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory);
	for (const char* name : { "wood_albedo.png", "wood_specular.png", "brick_albedo.jpg", "brick_emissive.png", "notes.txt",
	                          "stray_specular.png", "_albedo.png" }) {
		std::ofstream(directory / name).put('x');
	}

	const std::vector<materials::Files> found = materials::discover(directory);
	check(found.size() == 2, "maps without an albedo texture and files without a name make no material");
	if (found.size() == 2) {
		check(found[0].name == "brick" && found[1].name == "wood", "materials are sorted by name");
		check(found[0].albedo.filename() == "brick_albedo.jpg", "any image extension is accepted");
		check(found[0].specular.empty() && found[0].emissive.filename() == "brick_emissive.png",
		      "emissive map is attached to its material");
		check(found[1].specular.filename() == "wood_specular.png" && found[1].emissive.empty(),
		      "specular map is attached to its material and a missing map stays empty");
	}
	std::filesystem::remove_all(directory);

	check(materials::discover(directory / "missing").empty(), "a missing folder gives no materials");

	// Материалы, которые лежат в репозитории.
	const std::vector<materials::Files> shipped = materials::discover("textures");
	check(shipped.size() >= 2, "the repository ships several textures");
	bool has_specular = false;
	bool has_emissive = false;
	for (const materials::Files& files : shipped) {
		image::Image albedo;
		check(image::load(files.albedo, albedo) && albedo.width > 0, "every shipped albedo texture loads");

		// Карты блика и свечения лежат в тех же текстурных координатах, поэтому размеры совпадают с альбедо.
		for (const std::filesystem::path& map : { files.specular, files.emissive }) {
			if (map.empty()) {
				continue;
			}
			image::Image extra;
			check(image::load(map, extra) && extra.width == albedo.width && extra.height == albedo.height,
			      "specular and emissive maps have the size of the albedo texture");
		}
		has_specular = has_specular || !files.specular.empty();
		has_emissive = has_emissive || !files.emissive.empty();
	}
	check(has_specular && has_emissive, "the repository ships specular and emissive maps");

	// Карта свечения лавы: тёмная почти везде и яркая в трещинах, иначе «свечения в темноте» не будет.
	image::Image lava_emissive;
	if (image::load("textures/lava_emissive.png", lava_emissive)) {
		size_t dark = 0;
		size_t bright = 0;
		for (size_t i = 0; i < lava_emissive.pixels.size(); i += 4) {
			const int red = lava_emissive.pixels[i];
			dark += red < 16 ? 1 : 0;
			bright += red > 200 ? 1 : 0;
		}
		const size_t count = lava_emissive.pixels.size() / 4;
		check(dark > count / 4 && bright > count / 50, "lava emissive map has dark areas and bright cracks");
	} else {
		check(false, "lava emissive map loads");
	}
}

void testMaterialAssignment() {
	scene::Scene state;
	state.material_count = 3;
	for (int i = 0; i < 4; ++i) {
		scene::addObject(state);
	}
	check(state.objects[1].material.texture == 1 && state.objects[2].material.texture == 2 &&
	      state.objects[3].material.texture == 0, "new objects take materials in turn and wrap around");

	for (int i = 0; i < state.object_count; ++i) {
		check(state.objects[i].material.texture >= 0 && state.objects[i].material.texture < state.material_count,
		      "object material is inside the list");
	}

	scene::Scene empty;
	empty.material_count = 0;
	scene::addObject(empty);
	check(empty.objects[1].material.texture == 0, "without materials every object uses the first one");
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

void testSpotLightList() {
	scene::Scene state;
	check(state.spot_lights.empty(), "a scene starts without spot lights");

	for (int i = 0; i < 20; ++i) {
		scene::addSpotLight(state);
	}
	check(int(state.spot_lights.size()) == scene::max_spot_lights, "no more than 16 spot lights");
	check(state.selected_spot == scene::max_spot_lights - 1, "added spot light becomes selected");

	for (const scene::SpotLight& light : state.spot_lights) {
		check(light.inner_degrees <= light.outer_degrees && light.outer_degrees < 90.0f,
		      "inner angle does not exceed the outer one");
	}

	state.selected_spot = 1;
	const glm::vec3 next_position = state.spot_lights[2].position;
	scene::removeSelectedSpotLight(state);
	check(state.spot_lights[1].position == next_position, "removal shifts the following spot lights");

	while (!state.spot_lights.empty()) {
		state.selected_spot = int(state.spot_lights.size()) - 1;
		scene::removeSelectedSpotLight(state);
	}
	check(state.selected_spot == 0, "all spot lights can be removed");
}

void testDefaultScene() {
	const scene::Scene state = scene::makeDefaultScene();
	check(state.object_count >= 1 && state.object_count <= scene::max_objects, "default scene has objects");
	check(state.sun.enabled && state.sun.intensity > 0.0f, "default scene is lit by the directional light");
}

void testAnimatedSampling() {
	const scene::Scene state = scene::makeDefaultScene();
	int animated = 0;
	for (int i = 0; i < state.object_count; ++i) {
		animated += state.objects[i].animated_sampling ? 1 : 0;
	}
	check(animated == 1, "exactly one object of the default scene uses animated sampling");

	scene::Scene added;
	scene::addObject(added);
	check(!added.objects[1].animated_sampling, "new objects start with plain sampling");
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
	testCubeAndCone();
	testTextureCoordinates();
	testImages();
	testMaterials();
	testMaterialAssignment();
	testLightVisualization();
	testNormalMatrix();
	testCamera();
	testPointLightList();
	testSpotLightList();
	testDefaultScene();
	testTrajectoryAndAnimation();
	testObjectMatrices();
	testObjectList();
	testAnimatedSampling();

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
