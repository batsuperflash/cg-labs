#version 450

// Раскладка std140 должна совпадать с ObjectUniforms в application.cpp.
layout(set = 0, binding = 0) uniform ObjectUniforms {
	mat4 model;
	mat4 view;
	mat4 proj;
	vec4 color;  // цвет из интерфейса, в линейном пространстве
	uvec4 flags; // x != 0: использовать цвета вершин
} object;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;

void main() {
	gl_Position = object.proj * object.view * object.model * vec4(in_position, 1.0);

	vec3 base_color = object.flags.x != 0u ? in_color : vec3(1.0);
	out_color = base_color * object.color.rgb;
}
