#version 330 core
// 地面顶点着色器
layout(location = 0) in vec3 InPosition;

uniform mat4 ViewMatrix;
uniform mat4 ProjectionMatrix;
uniform mat4 LightMatrix;

out vec3 WorldPosition;
out vec4 LightSpacePosition;

void main()
{
    WorldPosition = InPosition;
    LightSpacePosition = LightMatrix * vec4(InPosition, 1.0);
    gl_Position = ProjectionMatrix * ViewMatrix * vec4(InPosition, 1.0);
}
