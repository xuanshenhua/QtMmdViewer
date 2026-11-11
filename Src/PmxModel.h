#ifndef PMXMODEL_H
#define PMXMODEL_H

#include <QQuaternion>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

// 骨骼标志位（PMX 规范）
enum PmxBoneFlag
{
    BoneFlagTailIsBone = 0x0001,
    BoneFlagRotatable = 0x0002,
    BoneFlagTranslatable = 0x0004,
    BoneFlagVisible = 0x0008,
    BoneFlagEnabled = 0x0010,
    BoneFlagIk = 0x0020,
    BoneFlagInheritLocal = 0x0080,
    BoneFlagInheritRotation = 0x0100,
    BoneFlagInheritTranslation = 0x0200,
    BoneFlagFixedAxis = 0x0400,
    BoneFlagLocalAxis = 0x0800,
    BoneFlagAfterPhysics = 0x1000,
    BoneFlagExternalParent = 0x2000
};

// 材质绘制标志位
enum PmxMaterialFlag
{
    MaterialFlagDoubleSided = 0x01,
    MaterialFlagGroundShadow = 0x02,
    MaterialFlagCastShadow = 0x04,
    MaterialFlagReceiveShadow = 0x08,
    MaterialFlagEdge = 0x10
};

// 表情（变形）类型
enum PmxMorphType
{
    MorphTypeGroup = 0,
    MorphTypeVertex = 1,
    MorphTypeBone = 2,
    MorphTypeUv = 3,
    MorphTypeExtraUv1 = 4,
    MorphTypeExtraUv2 = 5,
    MorphTypeExtraUv3 = 6,
    MorphTypeExtraUv4 = 7,
    MorphTypeMaterial = 8,
    MorphTypeFlip = 9,
    MorphTypeImpulse = 10
};

// 顶点：位置、法线、纹理坐标与最多 4 个骨骼权重（SDEF 按 BDEF2 处理）
struct PmxVertex
{
    QVector3D Position;
    QVector3D Normal;
    QVector2D Uv;
    int BoneIndex[4];
    float BoneWeight[4];
    float EdgeScale;
};

struct PmxMaterial
{
    QString Name;
    QString NameEn;
    QVector4D Diffuse;
    QVector3D Specular;
    float SpecularPower;
    QVector3D Ambient;
    int Flags;
    QVector4D EdgeColor;
    float EdgeSize;
    int TextureIndex;
    int SphereTextureIndex;
    int SphereMode; // 0 无，1 乘算，2 加算，3 子纹理
    bool SharedToon; // 为真时 ToonIndex 指向内置卡通纹理 toon01~toon10
    int ToonIndex;
    QString Memo;
    int IndexOffset;
    int IndexCount;
};

struct PmxIkLink
{
    int BoneIndex;
    bool HasLimit;
    QVector3D LimitMin;
    QVector3D LimitMax;
};

struct PmxBone
{
    QString Name;
    QString NameEn;
    QVector3D Position;
    int ParentIndex;
    int Layer;
    int Flags;
    int TailIndex;
    QVector3D TailOffset;
    int InheritParentIndex;
    float InheritWeight;
    QVector3D FixedAxis;
    QVector3D LocalAxisX;
    QVector3D LocalAxisZ;
    int ExternalKey;
    int IkTargetIndex;
    int IkLoopCount;
    float IkLimitAngle;
    QVector<PmxIkLink> IkLinks;
};

struct PmxVertexMorphOffset
{
    int VertexIndex;
    QVector3D Offset;
};

struct PmxUvMorphOffset
{
    int VertexIndex;
    QVector4D Offset;
};

struct PmxBoneMorphOffset
{
    int BoneIndex;
    QVector3D Translation;
    QQuaternion Rotation;
};

struct PmxMaterialMorphOffset
{
    int MaterialIndex; // -1 表示全部材质
    int Operation; // 0 乘算，1 加算
    QVector4D Diffuse;
    QVector3D Specular;
    float SpecularPower;
    QVector3D Ambient;
    QVector4D EdgeColor;
    float EdgeSize;
    QVector4D TextureTint;
    QVector4D SphereTint;
    QVector4D ToonTint;
};

struct PmxGroupMorphOffset
{
    int MorphIndex;
    float Weight;
};

struct PmxMorph
{
    QString Name;
    QString NameEn;
    int Panel;
    int Type;
    QVector<PmxVertexMorphOffset> VertexOffsets;
    QVector<PmxUvMorphOffset> UvOffsets;
    QVector<PmxBoneMorphOffset> BoneOffsets;
    QVector<PmxMaterialMorphOffset> MaterialOffsets;
    QVector<PmxGroupMorphOffset> GroupOffsets;
};

// 刚体：形状 0 球体，1 长方体，2 胶囊；模式 0 跟随骨骼，1 物理，2 物理+骨骼位置
struct PmxRigidBody
{
    QString Name;
    QString NameEn;
    int BoneIndex;
    int Group;
    int CollisionMask;
    int Shape;
    QVector3D Size;
    QVector3D Position;
    QVector3D Rotation;
    float Mass;
    float LinearDamping;
    float AngularDamping;
    float Restitution;
    float Friction;
    int Mode;
};

// 关节：带弹簧的六自由度约束
struct PmxJoint
{
    QString Name;
    QString NameEn;
    int Type;
    int RigidBodyA;
    int RigidBodyB;
    QVector3D Position;
    QVector3D Rotation;
    QVector3D LinearMin;
    QVector3D LinearMax;
    QVector3D AngularMin;
    QVector3D AngularMax;
    QVector3D LinearSpring;
    QVector3D AngularSpring;
};

// PMX 2.0 / 2.1 模型。读取时已从 MMD 的左手坐标系转换为 OpenGL 的右手坐标系（Z 轴取反）。
class PmxModel
{
public:
    PmxModel();

    bool Load(const QString& FilePath, QString* ErrorMessage);
    void Clear();

    QString FilePath;
    QString Directory;
    float Version;
    QString Name;
    QString NameEn;
    QString Comment;
    QString CommentEn;

    QVector<PmxVertex> Vertices;
    QVector<quint32> Indices;
    QStringList Textures;
    QVector<PmxMaterial> Materials;
    QVector<PmxBone> Bones;
    QVector<PmxMorph> Morphs;
    QVector<PmxRigidBody> RigidBodies;
    QVector<PmxJoint> Joints;
};

#endif
