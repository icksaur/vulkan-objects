#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec4 outShadowCoord;
layout(location = 4) out vec3 outWorldPos;

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
    uint tlasRID;
    uint useRT;
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
    vec3 normal = vec3(
        storageBuffers[nonuniformEXT(rid)].data[offset + 3],
        storageBuffers[nonuniformEXT(rid)].data[offset + 4],
        storageBuffers[nonuniformEXT(rid)].data[offset + 5]);
    vec2 uv = vec2(
        storageBuffers[nonuniformEXT(rid)].data[offset + 6],
        storageBuffers[nonuniformEXT(rid)].data[offset + 7]);

    gl_Position = viewProjection * vec4(pos, 1.0);
    outNormal = normal;
    outUV = uv;
    outWorldPos = pos;
    outShadowCoord = lightViewProjection * vec4(pos, 1.0);
}
