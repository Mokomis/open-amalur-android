#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 highlights;
layout(set=0,binding=0) uniform sampler2D scene;
layout(set=0,binding=1) uniform sampler2D effects;
layout(set=0,binding=2) uniform sampler2D bloom;
layout(set=0,binding=3) uniform sampler2D corrected;
layout(push_constant) uniform Merge { float bloomStrength; } merge;
void main() {
    vec4 fx = texture(effects, uv);
    vec3 raw = mix(texture(scene, uv).rgb, fx.rgb, fx.a)
             + texture(bloom, uv).rgb * merge.bloomStrength;
    float overflow = max(max(raw.r, raw.g), raw.b) - 1.0;
    vec3 hue = raw / max(max(max(raw.r, raw.g), raw.b), 1.0);
    float gradedLuma = dot(texture(corrected, uv).rgb, vec3(0.299, 0.587, 0.114));
    highlights = vec4(hue * max(overflow, 0.0), gradedLuma);
}
