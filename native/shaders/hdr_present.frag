#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0,binding=0) uniform sampler2D backbuffer;
layout(set=0,binding=1) uniform sampler2D highlights;
layout(push_constant) uniform Output { float hasHighlights; float paperWhite; } outputMode;
vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}
vec3 pq(vec3 nits) {
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(2610.0 / 16384.0));
    return pow((0.8359375 + 18.8515625 * y) / (1.0 + 18.6875 * y), vec3(2523.0 / 32.0));
}
void main() {
    vec3 sdr = texture(backbuffer, uv).rgb;
    const float peakNits = 1250.0;
    vec3 nits = srgbToLinear(sdr) * outputMode.paperWhite;
    if (outputMode.hasHighlights > 0.5) {
        vec4 extra = texture(highlights, uv);
        float difference = abs(dot(sdr, vec3(0.299, 0.587, 0.114)) - extra.a);
        float weight = 1.0 - smoothstep(0.055, 0.18, difference);
        float peak = max(max(extra.r, extra.g), extra.b);
        vec3 boost = extra.rgb * (3800.0 / (1.0 + peak * 5.5));
        float headroom = max(peakNits - max(max(nits.r, nits.g), nits.b), 0.0);
        float boostPeak = max(max(boost.r, boost.g), boost.b);
        nits += weight * boost * headroom / (headroom + boostPeak);
    }
    const mat3 bt2020 = mat3(
        0.627404, 0.069097, 0.016391,
        0.329283, 0.919540, 0.088013,
        0.043313, 0.011362, 0.895595
    );
    vec3 rgb2020 = bt2020 * nits;
    color = vec4(pq(min(rgb2020, vec3(peakNits))), 1.0);
}
