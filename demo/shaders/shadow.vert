#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(set=0, binding=0) buffer StorageBuffers {
    float data[];
} storageBuffers[];

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    uint vertexBufferRID;
    uint textureRID;
    uint shadowMapRID;
    uint lightBufferRID;
    float rotationAngle;
};

const uint FLOATS_PER_VERT = 8;

void main() {
    uint offset = uint(gl_VertexIndex) * FLOATS_PER_VERT;
    uint rid = vertexBufferRID;

    mat4 lightViewProjection;
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
            lightViewProjection[col][row] = storageBuffers[nonuniformEXT(lightBufferRID)].data[col * 4 + row];

    vec3 pos = vec3(
        storageBuffers[nonuniformEXT(rid)].data[offset],
        storageBuffers[nonuniformEXT(rid)].data[offset + 1],
        storageBuffers[nonuniformEXT(rid)].data[offset + 2]);

    gl_Position = lightViewProjection * vec4(pos, 1.0);
}
