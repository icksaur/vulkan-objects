#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(set=0, binding=0) buffer StorageBuffers {
    float data[];
} storageBuffers[];

layout(push_constant) uniform PushConstants {
    uint vertexBufferRID;
    float depth;
    uint color;
    uint pad;
};

layout(location = 0) out vec3 outColor;

void main() {
    uint base = uint(gl_VertexIndex) * 2u;
    float x = storageBuffers[nonuniformEXT(vertexBufferRID)].data[base];
    float y = storageBuffers[nonuniformEXT(vertexBufferRID)].data[base + 1u];
    gl_Position = vec4(x, y, depth, 1.0);
    outColor = vec3(
        float(color & 0xffu),
        float((color >> 8u) & 0xffu),
        float((color >> 16u) & 0xffu)) / 255.0;
}
