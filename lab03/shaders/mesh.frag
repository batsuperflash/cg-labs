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
	vec4 texture_params;
} object;

// Текстура альбедо лежит в том же наборе дескрипторов, что и динамический uniform buffer модели.
layout(set = 1, binding = 1) uniform sampler2D albedo_map;

// Массивы источников лежат в storage buffer, поэтому размер массива в шейдере не задан:
// сколько источников, говорит scene.counts.
struct PointLight {
	vec4 position; // xyz
	vec4 radiance; // rgb = цвет * интенсивность
};

layout(std430, set = 0, binding = 1) readonly buffer PointLights {
	PointLight lights[];
} point_lights;

struct SpotLight {
	vec4 position;  // xyz
	vec4 direction; // xyz = куда светит прожектор, единичный вектор
	vec4 radiance;  // rgb = цвет * интенсивность
	vec4 cone;      // x = cos(внутренний угол), y = cos(внешний угол)
};

layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
	SpotLight lights[];
} spot_lights;

layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec3 in_world_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

// Модель Блинна-Фонга для одного источника: N — нормаль, V — на камеру, L — на источник,
// radiance — приходящий свет (цвет * интенсивность, с учётом затухания).
vec3 blinnPhong(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo) {
	float n_dot_l = max(dot(N, L), 0.0);
	vec3 H = normalize(L + V);
	float specular = n_dot_l > 0.0 ? pow(max(dot(N, H), 0.0), object.specular.w) : 0.0;

	return (albedo * n_dot_l + object.specular.rgb * specular) * radiance;
}

void main() {
	vec3 N = normalize(in_world_normal);
	vec3 V = normalize(scene.camera_position.xyz - in_world_position);

	// Цвет поверхности: текстура, умноженная на оттенок модели.
	vec3 albedo = texture(albedo_map, in_uv).rgb * object.albedo.rgb;

	vec3 color = albedo * scene.ambient.rgb;

	if (scene.sun_direction.w > 0.5) {
		color += blinnPhong(N, V, -scene.sun_direction.xyz, scene.sun_radiance.rgb, albedo);
	}

	for (uint i = 0u; i < scene.counts.x; ++i) {
		PointLight light = point_lights.lights[i];

		vec3 to_light = light.position.xyz - in_world_position;
		float distance_squared = max(dot(to_light, to_light), 1e-4);

		// Закон обратных квадратов: яркость убывает как 1 / r².
		color += blinnPhong(N, V, to_light * inversesqrt(distance_squared), light.radiance.rgb / distance_squared, albedo);
	}

	for (uint i = 0u; i < scene.counts.y; ++i) {
		SpotLight light = spot_lights.lights[i];

		vec3 to_light = light.position.xyz - in_world_position;
		float distance_squared = max(dot(to_light, to_light), 1e-4);
		vec3 L = to_light * inversesqrt(distance_squared);

		// Косинус угла между осью прожектора и направлением от него на точку. Внутри внутреннего конуса
		// множитель равен 1, за внешним конусом 0, между ними плавно (smoothstep) меняется.
		float cos_angle = dot(-L, light.direction.xyz);
		float t = clamp((cos_angle - light.cone.y) / max(light.cone.x - light.cone.y, 1e-4), 0.0, 1.0);
		float edge = t * t * (3.0 - 2.0 * t);

		color += blinnPhong(N, V, L, light.radiance.rgb * (edge / distance_squared), albedo);
	}

	out_color = vec4(color, 1.0);
}
