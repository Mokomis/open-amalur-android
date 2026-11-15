#version 450
layout(location=0) in vec2 uv;
layout(location=1) in vec4 color;
layout(location=2) flat in int kind;
layout(location=0) out vec4 pixel;
layout(set=0,binding=0) uniform sampler2D atlas;
layout(set=0,binding=1) uniform sampler2D field;
layout(set=0,binding=2) uniform sampler2D title;
void main() {
    if (kind == 1) {
        pixel = texture(atlas, uv) * color;
    } else if (kind == 2) {
        float distance = texture(field, uv).r;
        float edge = max(fwidth(distance) * 0.75, 0.02);
        pixel = vec4(color.rgb, color.a * smoothstep(0.5 - edge, 0.5 + edge, distance));
    } else if (kind == 3) {
        pixel = vec4(texture(title, uv).rgb, 1.0) * color;
    } else {
        pixel = color;
    }
}
