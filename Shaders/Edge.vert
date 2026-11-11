#version 330 core
// 描边顶点着色器：沿法线方向外扩顶点，配合正面剔除绘制轮廓线
layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InUv;
layout(location = 3) in ivec4 InBoneIndex;
layout(location = 4) in vec4 InBoneWeight;
layout(location = 5) in float InEdgeScale;

uniform mat4 ViewMatrix;
uniform mat4 ProjectionMatrix;
uniform vec3 CameraPosition;
uniform float EdgeSize;
uniform float EdgeFactor;

uniform sampler2D BoneTexture;
uniform int BoneTextureWidth;

// 从浮点纹理中读取第 Index 根骨骼的蒙皮矩阵（每个矩阵占 4 个像素，按列存放）
mat4 FetchBone(int Index)
{
    int Base = Index * 4;
    return mat4(texelFetch(BoneTexture, ivec2((Base + 0) % BoneTextureWidth, (Base + 0) / BoneTextureWidth), 0),
                texelFetch(BoneTexture, ivec2((Base + 1) % BoneTextureWidth, (Base + 1) / BoneTextureWidth), 0),
                texelFetch(BoneTexture, ivec2((Base + 2) % BoneTextureWidth, (Base + 2) / BoneTextureWidth), 0),
                texelFetch(BoneTexture, ivec2((Base + 3) % BoneTextureWidth, (Base + 3) / BoneTextureWidth), 0));
}

mat4 SkinMatrix()
{
    return FetchBone(InBoneIndex.x) * InBoneWeight.x
         + FetchBone(InBoneIndex.y) * InBoneWeight.y
         + FetchBone(InBoneIndex.z) * InBoneWeight.z
         + FetchBone(InBoneIndex.w) * InBoneWeight.w;
}

void main()
{
    mat4 Skin = SkinMatrix();
    vec4 World = Skin * vec4(InPosition, 1.0);
    vec3 Normal = normalize(mat3(Skin) * InNormal);
    float Distance = length(CameraPosition - World.xyz);
    World.xyz += Normal * (EdgeSize * InEdgeScale * EdgeFactor * Distance);
    gl_Position = ProjectionMatrix * ViewMatrix * World;
}
