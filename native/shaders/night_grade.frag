#version 450

layout(set=0, binding=0) uniform sampler2D scene;
layout(push_constant) uniform NightGrade { float strength; } night;
layout(location=0) out vec4 color;

void main()
{
    color = texelFetch(scene, ivec2(gl_FragCoord.xy), 0);
    float luminance = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    float peak = max(color.r, max(color.g, color.b));
    float shadows = 1.0 - smoothstep(0.07, 0.56, luminance);
    float protectHighlights = 1.0 - smoothstep(0.38, 0.72, peak);
    color.rgb *= 1.0 - night.strength * shadows * protectHighlights;
}
