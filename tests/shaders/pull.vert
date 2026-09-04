#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Vertex pulling: no vertex attributes. Positions come from a storage buffer
// addressed by RID, indexed by gl_VertexIndex.

layout(set=0, binding=0) buffer StorageBuffers {
    float data[];
} storageBuffers[];

layout(push_constant) uniform PushConstants {
    uint vertexBufferRID;
    uint pad0;
    uint pad1;
    uint pad2;
};

layout(location = 0) out vec3 outColor;

void main() {
    uint base = uint(gl_VertexIndex) * 2u;
    float x = storageBuffers[nonuniformEXT(vertexBufferRID)].data[base];
    float y = storageBuffers[nonuniformEXT(vertexBufferRID)].data[base + 1u];
    gl_Position = vec4(x, y, 0.0, 1.0);
    outColor = vec3(0.0, 1.0, 0.0);
}
