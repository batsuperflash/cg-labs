#version 450

// Неосвещаемый цвет: значки источников света и контуры прожекторов. Берёт albedo модели как есть,
// никакой модели освещения здесь нет. Вершинный шейдер тот же, что и у освещаемых моделей.
layout(set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	mat4 normal_matrix;
	vec4 albedo;
	vec4 specular;
	vec4 texture_params;
} object;

layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec3 in_world_normal;

layout(location = 0) out vec4 out_color;

void main() {
	out_color = vec4(object.albedo.rgb, 1.0);
}
