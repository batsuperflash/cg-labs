#version 450
layout(location = 0) in vec3 position;
layout(push_constant) uniform ShadowTransform {
    mat4 light_mvp;
} shadow;

void main() {
    gl_Position = shadow.light_mvp * vec4(position, 1.0);
}
