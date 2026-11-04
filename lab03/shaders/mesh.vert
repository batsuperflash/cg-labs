#version 450

// Раскладка std140 должна совпадать со SceneUniforms в application.cpp.
layout(set = 0, binding = 0) uniform SceneUniforms {
	mat4 view;
	mat4 proj;
	vec4 camera_position; // xyz
	vec4 ambient;         // rgb = цвет * интенсивность
	vec4 sun_direction;   // xyz = куда идёт свет, w = 1, если включён
	vec4 sun_radiance;    // rgb = цвет * интенсивность
	uvec4 counts;         // x = число точечных источников, y = число прожекторов
} scene;

// Раскладка std140 должна совпадать с ModelUniforms в application.cpp.
// Это один элемент динамического uniform buffer: его смещение задаётся при привязке набора дескрипторов.
layout(set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	mat4 normal_matrix;
	vec4 albedo;         // множитель цвета текстуры (оттенок)
	vec4 specular;       // rgb = цвет блика, w = коэффициент блика
	vec4 texture_params; // xy = во сколько раз повторить текстуру по u и v, z = множитель карты свечения
} object;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec3 out_world_position;
layout(location = 1) out vec3 out_world_normal;
layout(location = 2) out vec2 out_uv;

void main() {
	vec4 world_position = object.model * vec4(in_position, 1.0);

	out_world_position = world_position.xyz;
	out_world_normal = mat3(object.normal_matrix) * in_normal;
	out_uv = in_uv * object.texture_params.xy;
	gl_Position = scene.proj * scene.view * world_position;
}
