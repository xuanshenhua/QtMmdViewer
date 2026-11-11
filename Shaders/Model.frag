#version 330 core
// 模型片段着色器：MMD 风格的卡通着色（环境色 + 漫反射 + 纹理 + 球面贴图 + Toon + 高光 + 自阴影）
in vec3 WorldPosition;
in vec3 WorldNormal;
in vec3 ViewNormal;
in vec2 TexCoord;
in vec4 LightSpacePosition;

uniform vec4 DiffuseColor;
uniform vec3 AmbientColor;
uniform vec3 SpecularColor;
uniform float SpecularPower;
uniform vec3 LightDirection;
uniform vec3 LightColor;
uniform vec3 CameraPosition;

uniform sampler2D DiffuseTexture;
uniform sampler2D SphereTexture;
uniform sampler2D ToonTexture;
uniform sampler2DShadow ShadowMap;

uniform int UseTexture;
uniform int SphereMode;
uniform int UseToon;
uniform int UseShadow;

uniform vec4 TextureMul;
uniform vec4 TextureAdd;
uniform vec4 SphereMul;
uniform vec4 SphereAdd;
uniform vec4 ToonMul;
uniform vec4 ToonAdd;

out vec4 FragColor;

// 3x3 PCF 阴影采样，返回可见度（1 为完全受光）
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
            Sum += texture(ShadowMap, vec3(Projected.xy + vec2(X, Y) * TexelSize, Projected.z - 0.0015));
        }
    }
    return Sum / 9.0;
}

void main()
{
    vec3 Normal = normalize(WorldNormal);
    if (!gl_FrontFacing) {
        Normal = -Normal;
    }
    vec3 ToLight = normalize(-LightDirection);
    vec3 ToEye = normalize(CameraPosition - WorldPosition);

    vec4 Color = vec4(clamp(AmbientColor + DiffuseColor.rgb * LightColor, 0.0, 1.0), DiffuseColor.a);
    if (UseTexture != 0) {
        vec4 TextureColor = clamp(texture(DiffuseTexture, TexCoord) * TextureMul + TextureAdd, 0.0, 1.0);
        Color *= TextureColor;
    }
    if (SphereMode > 0) {
        vec3 N = normalize(ViewNormal);
        vec2 SphereUv = vec2(N.x * 0.5 + 0.5, 0.5 - N.y * 0.5);
        vec4 SphereColor = texture(SphereTexture, SphereUv) * SphereMul + SphereAdd;
        if (SphereMode == 2) {
            Color.rgb += SphereColor.rgb;
        } else {
            Color.rgb *= SphereColor.rgb;
        }
    }

    float LightNormal = dot(Normal, ToLight);
    if (UseShadow != 0) {
        LightNormal = min(LightNormal, ShadowVisibility() * 2.0 - 1.0);
    }
    if (UseToon != 0) {
        vec4 ToonColor = texture(ToonTexture, vec2(0.5, clamp(0.5 - LightNormal * 0.5, 0.0, 1.0))) * ToonMul + ToonAdd;
        Color.rgb *= ToonColor.rgb;
    }

    if (SpecularPower > 0.0) {
        vec3 Half = normalize(ToLight + ToEye);
        float Specular = pow(max(dot(Normal, Half), 0.0), SpecularPower);
        Color.rgb += SpecularColor * LightColor * Specular;
    }

    if (Color.a < 0.004) {
        discard;
    }
    FragColor = clamp(Color, 0.0, 1.0);
}
