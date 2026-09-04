#version 450

// Not for a pipeline. Exists so a test can prove GraphicsPipelineBuilder rejects a vertex
// shader that declares vertex attribute inputs: this library supplies no vertex input state,
// so such a shader would silently read undefined data.

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 outColor;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    outColor = inPosition;
}
