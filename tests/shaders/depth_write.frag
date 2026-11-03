#version 450

layout(location = 0) in vec3 inColor;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    uint vertexBufferRID;
    float depth;
    uint color;
    uint pad;
};

void main() {
    outColor = vec4(inColor, 1.0);
}
