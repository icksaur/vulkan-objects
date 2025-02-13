#version 450
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;  

layout(location = 1) out vec2 uv;

layout(std140, binding = 0) uniform matrixBuffer {
    mat4 viewProjection;
    float zScale;
};

void main() {
    uv = inUV;
    gl_Position = viewProjection * vec4(inPos, 1.0);
}