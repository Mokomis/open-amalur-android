#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec2 coords;
layout(location=2) in vec4 tint;
layout(location=3) in float mode;
layout(location=0) out vec2 uv;
layout(location=1) out vec4 color;
layout(location=2) flat out int kind;
layout(push_constant) uniform Viewport { vec2 size; } view;
void main() {
    gl_Position = vec4(position / view.size * 2.0 - 1.0, 0.0, 1.0);
    uv = coords;
    color = tint;
    kind = int(mode + 0.5);
}
