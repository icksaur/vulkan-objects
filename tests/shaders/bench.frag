#version 450

layout(location = 0) out vec4 outColor;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

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

void main() {
    vec3 lightDir = normalize(vec3(1.0, -2.0, 1.0));
    float NdotL = max(dot(normalize(inNormal), -lightDir), 0.0);
    outColor = vec4(vec3(0.15 + 0.85 * NdotL) * vec3(inUV, 1.0), 1.0);
}
