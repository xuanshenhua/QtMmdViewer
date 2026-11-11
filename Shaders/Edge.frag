#version 330 core
// 描边片段着色器：输出材质的描边颜色
uniform vec4 EdgeColor;
out vec4 FragColor;

void main()
{
    FragColor = EdgeColor;
}
