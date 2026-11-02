#version 450

// Те же блоки, что и в mesh.vert.
layout(set = 0, binding = 0) uniform SceneUniforms {
	mat4 view;
	mat4 proj;
	vec4 camera_position;
	vec4 ambient;
	vec4 sun_direction;
	vec4 sun_radiance;
	uvec4 counts;
} scene;

layout(set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	mat4 normal_matrix;
	vec4 albedo;
	vec4 specular;
} object;

// Массивы источников лежат в storage buffer, поэтому размер массива в шейдере не задан:
// сколько источников, говорит scene.counts.
struct PointLight {
	vec4 position; // xyz
	vec4 radiance; // rgb = цвет * интенсивность
};

layout(std430, set = 0, binding = 1) readonly buffer PointLights {
	PointLight lights[];
} point_lights;

layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec3 in_world_normal;

layout(location = 0) out vec4 out_color;

// Модель Блинна-Фонга для одного источника: N — нормаль, V — на камеру, L — на источник,
// radiance — приходящий свет (цвет * интенсивность, с учётом затухания).
vec3 blinnPhong(vec3 N, vec3 V, vec3 L, vec3 radiance) {
	float n_dot_l = max(dot(N, L), 0.0);
	vec3 H = normalize(L + V);
	float specular = n_dot_l > 0.0 ? pow(max(dot(N, H), 0.0), object.specular.w) : 0.0;

	return (object.albedo.rgb * n_dot_l + object.specular.rgb * specular) * radiance;
}

void main() {
	vec3 N = normalize(in_world_normal);
	vec3 V = normalize(scene.camera_position.xyz - in_world_position);

	vec3 color = object.albedo.rgb * scene.ambient.rgb;

	if (scene.sun_direction.w > 0.5) {
		color += blinnPhong(N, V, -scene.sun_direction.xyz, scene.sun_radiance.rgb);
	}

	for (uint i = 0u; i < scene.counts.x; ++i) {
		PointLight light = point_lights.lights[i];

		vec3 to_light = light.position.xyz - in_world_position;
		float distance_squared = max(dot(to_light, to_light), 1e-4);

		// Закон обратных квадратов: яркость убывает как 1 / r².
		color += blinnPhong(N, V, to_light * inversesqrt(distance_squared), light.radiance.rgb / distance_squared);
	}

	out_color = vec4(color, 1.0);
}
