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

// Текстуры материала: у каждого материала свой набор дескрипторов (set = 2), он выбирается при рисовании модели.
layout(set = 2, binding = 0) uniform sampler2D albedo_map;

// Push-константы: время и флаги нетривиального сэмплирования. Раскладка совпадает с PushConstants в application.cpp.
layout(push_constant) uniform PushConstants {
	float time; // секунды с запуска программы
	uint flags;
} push;

const uint FLAG_ANIMATED_SAMPLING = 1u;

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

// Нетривиальное сэмплирование: одна и та же текстура читается три раза с разными координатами, а оттенок меняется со временем.
vec3 sampleAnimated(vec2 uv, float t) {
	// 1. Координаты сдвигаются: текстура плывёт по диагонали.
	vec3 drift = texture(albedo_map, uv + vec2(0.08, 0.05) * t).rgb;

	// 2. Координаты вращаются вокруг центра грани, масштаб пульсирует.
	float angle = 0.35 * t;
	mat2 rotation = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
	vec2 spin_uv = rotation * (uv - 0.5) * (1.6 + 0.5 * sin(0.7 * t)) + 0.5;
	vec3 spin = texture(albedo_map, spin_uv).rgb;

	// 3. Координаты искажаются бегущей волной.
	vec2 wave_uv = uv + 0.025 * vec2(sin(20.0 * uv.y + 2.0 * t), cos(20.0 * uv.x + 2.0 * t));
	vec3 wave = texture(albedo_map, wave_uv).rgb;

	// Вес первого и второго образца меняется со временем, третий подмешивается всегда.
	float k = 0.5 + 0.5 * sin(0.9 * t);
	vec3 color = mix(drift, spin, k) * 0.65 + wave * 0.35;

	// Модуляция цвета: три канала колеблются со сдвигом по фазе, оттенок проходит по кругу.
	vec3 hue = 0.55 + 0.45 * cos(0.8 * t + vec3(0.0, 2.094, 4.188));
	return color * hue;
}

void main() {
	vec3 N = normalize(in_world_normal);
	vec3 V = normalize(scene.camera_position.xyz - in_world_position);

	// Цвет поверхности: текстура, умноженная на оттенок модели. Способ чтения текстуры выбирают флаги из push-констант.
	vec3 texel = (push.flags & FLAG_ANIMATED_SAMPLING) != 0u ? sampleAnimated(in_uv, push.time) : texture(albedo_map, in_uv).rgb;
	vec3 albedo = texel * object.albedo.rgb;

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
