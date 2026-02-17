#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) out vec4 outColor;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inShadowCoord;

layout(set=0, binding=1) uniform sampler2D samplers[];
layout(set=0, binding=1) uniform sampler2DShadow shadowSamplers[];

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    uint vertexBufferRID;
    uint textureRID;
    uint shadowMapRID;
    uint lightBufferRID;
    float rotationAngle;
};

void main() {
    // Light direction (matches the orthographic light in main.cpp)
    vec3 lightDir = normalize(vec3(1.0, -2.0, 1.0));

    // Diffuse lighting
    vec3 normal = normalize(inNormal);
    float NdotL = max(dot(normal, -lightDir), 0.0);

    // Shadow mapping
    // Perspective divide (ortho so w=1, but correct for generality)
    vec3 shadowNDC = inShadowCoord.xyz / inShadowCoord.w;
    // Map from [-1,1] to [0,1] for texture lookup
    vec2 shadowUV = shadowNDC.xy * 0.5 + 0.5;
    float shadowDepth = shadowNDC.z; // Already in [0,1] for Vulkan

    float shadow = 1.0;
    if (shadowUV.x >= 0.0 && shadowUV.x <= 1.0 && shadowUV.y >= 0.0 && shadowUV.y <= 1.0) {
        // sampler2DShadow returns 0.0 (in shadow) or 1.0 (lit) based on comparison
        shadow = texture(shadowSamplers[nonuniformEXT(shadowMapRID)], vec3(shadowUV, shadowDepth));
    }

    // Sample texture
    vec4 texColor = texture(samplers[nonuniformEXT(textureRID)], inUV);

    // Ambient + shadowed diffuse
    float ambient = 0.15;
    float lighting = ambient + (1.0 - ambient) * NdotL * shadow;

    outColor = vec4(texColor.rgb * lighting, texColor.a);
}
