#version 330 core
// 地面片段着色器：程序化网格线 + 接收阴影 + 远处淡出
in vec3 WorldPosition;
in vec4 LightSpacePosition;

uniform sampler2DShadow ShadowMap;
uniform int ShowGrid;
uniform int UseShadow;
uniform vec3 GroundColor;

out vec4 FragColor;

float ShadowVisibility()
{
    vec3 Projected = LightSpacePosition.xyz / LightSpacePosition.w * 0.5 + 0.5;
    if (Projected.x < 0.0 || Projected.x > 1.0 || Projected.y < 0.0 || Projected.y > 1.0 || Projected.z > 1.0) {
        return 1.0;
    }
    vec2 TexelSize = 1.0 / vec2(textureSize(ShadowMap, 0));
    float Sum = 0.0;
    for (int X = -1; X <= 1; ++X) {
        for (int Y = -1; Y <= 1; ++Y) {
            Sum += texture(ShadowMap, vec3(Projected.xy + vec2(X, Y) * TexelSize, Projected.z - 0.002));
        }
    }
    return Sum / 9.0;
}

float GridLine(vec2 Coordinate, float Spacing)
{
    vec2 Scaled = Coordinate / Spacing;
    vec2 Width = fwidth(Scaled);
    vec2 Line = abs(fract(Scaled - 0.5) - 0.5) / max(Width, vec2(1e-5));
    return 1.0 - min(min(Line.x, Line.y), 1.0);
}

void main()
{
    float Radius = length(WorldPosition.xz);
    float Fade = 1.0 - smoothstep(40.0, 90.0, Radius);
    vec3 Color = GroundColor;
    float Alpha = 0.85 * Fade;
    if (ShowGrid != 0) {
        float Line = GridLine(WorldPosition.xz, 5.0);
        Color = mix(Color, vec3(0.45, 0.47, 0.52), Line * 0.8);
        float AxisX = 1.0 - min(abs(WorldPosition.z) / max(fwidth(WorldPosition.z) * 1.5, 1e-5), 1.0);
        float AxisZ = 1.0 - min(abs(WorldPosition.x) / max(fwidth(WorldPosition.x) * 1.5, 1e-5), 1.0);
        Color = mix(Color, vec3(0.75, 0.3, 0.3), AxisX);
        Color = mix(Color, vec3(0.3, 0.45, 0.8), AxisZ);
    }
    if (UseShadow != 0) {
        Color *= mix(0.45, 1.0, ShadowVisibility());
    }
    FragColor = vec4(Color, Alpha);
}
