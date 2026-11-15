#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform sampler2D foreground;
layout(set = 0, binding = 1) uniform sampler2D waterfall;

float brightest(vec3 c)
{
    return max(max(c.r, c.g), c.b);
}

void main()
{
    vec4 character = texture(foreground, uv);
    vec2 pixel = 1.0 / vec2(textureSize(foreground, 0));
    float coverage = brightest(character.rgb);
    // Keep the character's antialiased silhouette and small dark armor details.
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            coverage = max(coverage, brightest(texture(foreground, uv + pixel * vec2(x, y)).rgb));
    float mask = smoothstep(0.002, 0.025, coverage);
    vec2 panel_size = vec2(textureSize(foreground, 0));
    vec2 scene_size = vec2(textureSize(waterfall, 0));
    float relative_aspect = panel_size.x * scene_size.y / (panel_size.y * scene_size.x);
    vec2 crop = min(vec2(1.0), vec2(relative_aspect, 1.0 / relative_aspect));
    vec2 scene_uv = (vec2(1.0 - uv.x, uv.y) - 0.5) * crop + 0.5;
    vec3 landscape = texture(waterfall, scene_uv).rgb;
    color = vec4(mix(landscape, character.rgb, mask), character.a);
}
