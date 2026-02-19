#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) out vec4 outColor;
layout(location = 1) in vec2 inUV;

layout(set=0, binding=1) uniform sampler2D samplers[];

layout(push_constant) uniform PushConstants {
    uint textureRID;
};

void main() {
    outColor = texture(samplers[nonuniformEXT(textureRID)], inUV);
}
