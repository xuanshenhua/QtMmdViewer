#version 330 core
// 阴影贴图顶点着色器：从光源视角渲染深度
layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InUv;
layout(location = 3) in ivec4 InBoneIndex;
layout(location = 4) in vec4 InBoneWeight;
layout(location = 5) in float InEdgeScale;

uniform mat4 LightMatrix;

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
    gl_Position = LightMatrix * (SkinMatrix() * vec4(InPosition, 1.0));
}
