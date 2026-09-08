#version 450

layout(location = 1) out vec2 outUV;

layout(push_constant) uniform PushConstants {
    uint textureRID;
};

void main() {
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 uvs[3] = vec2[3](vec2(0.0, 0.0), vec2(2.0, 0.0), vec2(0.0, 2.0));

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    outUV = uvs[gl_VertexIndex];
}
