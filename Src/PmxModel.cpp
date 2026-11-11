#include "PmxModel.h"

#include "BinaryReader.h"
#include "TextCodec.h"

#include <QFile>
#include <QFileInfo>

namespace
{
// 把 MMD 左手坐标系中的位置转换为右手坐标系
QVector3D FlipPosition(const QVector3D& Value)
{
    return QVector3D(Value.x(), Value.y(), -Value.z());
}

// 欧拉角（弧度）在 Z 轴镜像后，绕 X、Y 轴的角度取反
QVector3D FlipEuler(const QVector3D& Value)
{
    return QVector3D(-Value.x(), -Value.y(), Value.z());
}

// 角度范围镜像：X、Y 分量取反并交换上下限
void FlipAngleRange(QVector3D* Min, QVector3D* Max)
{
    QVector3D OldMin = *Min;
    QVector3D OldMax = *Max;
    *Min = QVector3D(-OldMax.x(), -OldMax.y(), OldMin.z());
    *Max = QVector3D(-OldMin.x(), -OldMin.y(), OldMax.z());
}

// 位置范围镜像：Z 分量取反并交换上下限
void FlipPositionRange(QVector3D* Min, QVector3D* Max)
{
    QVector3D OldMin = *Min;
    QVector3D OldMax = *Max;
    *Min = QVector3D(OldMin.x(), OldMin.y(), -OldMax.z());
    *Max = QVector3D(OldMax.x(), OldMax.y(), -OldMin.z());
}

void SetError(QString* ErrorMessage, const QString& Text)
{
    if (ErrorMessage) {
        *ErrorMessage = Text;
    }
}
}

PmxModel::PmxModel()
    : Version(0.0f)
{
}

void PmxModel::Clear()
{
    *this = PmxModel();
}

bool PmxModel::Load(const QString& Path, QString* ErrorMessage)
{
    Clear();
    QFile File(Path);
    if (!File.open(QIODevice::ReadOnly)) {
        SetError(ErrorMessage, QStringLiteral("无法打开模型文件：%1").arg(Path));
        return false;
    }
    QByteArray Data = File.readAll();
    File.close();

    FilePath = QFileInfo(Path).absoluteFilePath();
    Directory = QFileInfo(Path).absolutePath();

    BinaryReader Reader(Data);
    QByteArray Magic = Reader.ReadBytes(4);
    if (Magic != QByteArray("PMX ", 4)) {
        SetError(ErrorMessage, QStringLiteral("不是有效的 PMX 文件（仅支持 PMX 2.0/2.1，不支持 PMD）。"));
        return false;
    }
    Version = Reader.ReadF32();
    int GlobalCount = Reader.ReadU8();
    QByteArray Globals = Reader.ReadBytes(GlobalCount);
    if (!Reader.IsOk() || GlobalCount < 8) {
        SetError(ErrorMessage, QStringLiteral("PMX 文件头已损坏。"));
        return false;
    }
    const int Encoding = quint8(Globals.at(0));
    const int ExtraUvCount = quint8(Globals.at(1));
    const int VertexIndexSize = quint8(Globals.at(2));
    const int TextureIndexSize = quint8(Globals.at(3));
    const int MaterialIndexSize = quint8(Globals.at(4));
    const int BoneIndexSize = quint8(Globals.at(5));
    const int MorphIndexSize = quint8(Globals.at(6));
    const int RigidBodyIndexSize = quint8(Globals.at(7));

    // 读取带长度前缀的字符串
    auto ReadText = [&]() -> QString {
        qint32 Length = Reader.ReadI32();
        if (Length <= 0 || Length > Reader.Remaining()) {
            if (Length != 0) {
                Reader.Skip(-1);
            }
            return QString();
        }
        QByteArray Bytes = Reader.ReadBytes(Length);
        return Encoding == 0 ? TextCodec::DecodeUtf16Le(Bytes) : TextCodec::DecodeUtf8(Bytes);
    };
    auto Fail = [&](const QString& Section) -> bool {
        SetError(ErrorMessage, QStringLiteral("读取 PMX 数据失败：%1 部分不完整或已损坏。").arg(Section));
        return false;
    };

    Name = ReadText();
    NameEn = ReadText();
    Comment = ReadText();
    CommentEn = ReadText();

    // ---------- 顶点 ----------
    qint32 VertexCount = Reader.ReadI32();
    if (!Reader.IsOk() || VertexCount < 0) {
        return Fail(QStringLiteral("顶点"));
    }
    Vertices.resize(VertexCount);
    for (int I = 0; I < VertexCount && Reader.IsOk(); ++I) {
        PmxVertex& Vertex = Vertices[I];
        Vertex.Position = FlipPosition(Reader.ReadVec3());
        Vertex.Normal = FlipPosition(Reader.ReadVec3());
        Vertex.Uv = Reader.ReadVec2();
        Reader.Skip(16 * ExtraUvCount);
        for (int K = 0; K < 4; ++K) {
            Vertex.BoneIndex[K] = 0;
            Vertex.BoneWeight[K] = 0.0f;
        }
        int WeightType = Reader.ReadU8();
        switch (WeightType) {
        case 0: // BDEF1
            Vertex.BoneIndex[0] = Reader.ReadIndex(BoneIndexSize, false);
            Vertex.BoneWeight[0] = 1.0f;
            break;
        case 1: // BDEF2
            Vertex.BoneIndex[0] = Reader.ReadIndex(BoneIndexSize, false);
            Vertex.BoneIndex[1] = Reader.ReadIndex(BoneIndexSize, false);
            Vertex.BoneWeight[0] = Reader.ReadF32();
            Vertex.BoneWeight[1] = 1.0f - Vertex.BoneWeight[0];
            break;
        case 2: // BDEF4
        case 4: // QDEF（按 BDEF4 处理）
            for (int K = 0; K < 4; ++K) {
                Vertex.BoneIndex[K] = Reader.ReadIndex(BoneIndexSize, false);
            }
            for (int K = 0; K < 4; ++K) {
                Vertex.BoneWeight[K] = Reader.ReadF32();
            }
            break;
        case 3: // SDEF（简化为 BDEF2）
            Vertex.BoneIndex[0] = Reader.ReadIndex(BoneIndexSize, false);
            Vertex.BoneIndex[1] = Reader.ReadIndex(BoneIndexSize, false);
            Vertex.BoneWeight[0] = Reader.ReadF32();
            Vertex.BoneWeight[1] = 1.0f - Vertex.BoneWeight[0];
            Reader.Skip(36);
            break;
        default:
            return Fail(QStringLiteral("顶点权重"));
        }
        // 归一化权重，并把无效骨骼索引替换为 0
        float Sum = 0.0f;
        for (int K = 0; K < 4; ++K) {
            if (Vertex.BoneIndex[K] < 0) {
                Vertex.BoneIndex[K] = 0;
                Vertex.BoneWeight[K] = 0.0f;
            }
            Sum += Vertex.BoneWeight[K];
        }
        if (Sum > 1e-6f) {
            for (int K = 0; K < 4; ++K) {
                Vertex.BoneWeight[K] /= Sum;
            }
        } else {
            Vertex.BoneWeight[0] = 1.0f;
        }
        Vertex.EdgeScale = Reader.ReadF32();
    }
    if (!Reader.IsOk()) {
        return Fail(QStringLiteral("顶点"));
    }

    // ---------- 面（三角形索引） ----------
    qint32 IndexCount = Reader.ReadI32();
    if (!Reader.IsOk() || IndexCount < 0 || IndexCount % 3 != 0) {
        return Fail(QStringLiteral("面"));
    }
    Indices.resize(IndexCount);
    for (int I = 0; I < IndexCount; ++I) {
        Indices[I] = quint32(Reader.ReadIndex(VertexIndexSize, true));
    }
    // 镜像坐标后绕序反转，交换每个三角形的后两个索引以保持正面朝向
    for (int I = 0; I + 2 < IndexCount; I += 3) {
        quint32 Temp = Indices[I + 1];
        Indices[I + 1] = Indices[I + 2];
        Indices[I + 2] = Temp;
    }
    for (int I = 0; I < IndexCount; ++I) {
        if (Indices[I] >= quint32(VertexCount)) {
            Indices[I] = 0;
        }
    }

    // ---------- 纹理 ----------
    qint32 TextureCount = Reader.ReadI32();
    if (!Reader.IsOk() || TextureCount < 0) {
        return Fail(QStringLiteral("纹理"));
    }
    for (int I = 0; I < TextureCount; ++I) {
        Textures.append(ReadText());
    }

    // ---------- 材质 ----------
    qint32 MaterialCount = Reader.ReadI32();
    if (!Reader.IsOk() || MaterialCount < 0) {
        return Fail(QStringLiteral("材质"));
    }
    Materials.resize(MaterialCount);
    int Offset = 0;
    for (int I = 0; I < MaterialCount && Reader.IsOk(); ++I) {
        PmxMaterial& Material = Materials[I];
        Material.Name = ReadText();
        Material.NameEn = ReadText();
        Material.Diffuse = Reader.ReadVec4();
        Material.Specular = Reader.ReadVec3();
        Material.SpecularPower = Reader.ReadF32();
        Material.Ambient = Reader.ReadVec3();
        Material.Flags = Reader.ReadU8();
        Material.EdgeColor = Reader.ReadVec4();
        Material.EdgeSize = Reader.ReadF32();
        Material.TextureIndex = Reader.ReadIndex(TextureIndexSize, false);
        Material.SphereTextureIndex = Reader.ReadIndex(TextureIndexSize, false);
        Material.SphereMode = Reader.ReadU8();
        Material.SharedToon = Reader.ReadU8() != 0;
        if (Material.SharedToon) {
            Material.ToonIndex = Reader.ReadU8();
        } else {
            Material.ToonIndex = Reader.ReadIndex(TextureIndexSize, false);
        }
        Material.Memo = ReadText();
        Material.IndexCount = Reader.ReadI32();
        Material.IndexOffset = Offset;
        if (Material.IndexCount < 0 || Offset + Material.IndexCount > IndexCount) {
            Material.IndexCount = qMax(0, IndexCount - Offset);
        }
        Offset += Material.IndexCount;
    }
    if (!Reader.IsOk()) {
        return Fail(QStringLiteral("材质"));
    }

    // ---------- 骨骼 ----------
    qint32 BoneCount = Reader.ReadI32();
    if (!Reader.IsOk() || BoneCount < 0) {
        return Fail(QStringLiteral("骨骼"));
    }
    Bones.resize(BoneCount);
    for (int I = 0; I < BoneCount && Reader.IsOk(); ++I) {
        PmxBone& Bone = Bones[I];
        Bone.Name = ReadText();
        Bone.NameEn = ReadText();
        Bone.Position = FlipPosition(Reader.ReadVec3());
        Bone.ParentIndex = Reader.ReadIndex(BoneIndexSize, false);
        Bone.Layer = Reader.ReadI32();
        Bone.Flags = Reader.ReadU16();
        Bone.TailIndex = -1;
        Bone.InheritParentIndex = -1;
        Bone.InheritWeight = 0.0f;
        Bone.ExternalKey = 0;
        Bone.IkTargetIndex = -1;
        Bone.IkLoopCount = 0;
        Bone.IkLimitAngle = 0.0f;
        if (Bone.Flags & BoneFlagTailIsBone) {
            Bone.TailIndex = Reader.ReadIndex(BoneIndexSize, false);
        } else {
            Bone.TailOffset = FlipPosition(Reader.ReadVec3());
        }
        if (Bone.Flags & (BoneFlagInheritRotation | BoneFlagInheritTranslation)) {
            Bone.InheritParentIndex = Reader.ReadIndex(BoneIndexSize, false);
            Bone.InheritWeight = Reader.ReadF32();
        }
        if (Bone.Flags & BoneFlagFixedAxis) {
            Bone.FixedAxis = FlipPosition(Reader.ReadVec3());
        }
        if (Bone.Flags & BoneFlagLocalAxis) {
            Bone.LocalAxisX = FlipPosition(Reader.ReadVec3());
            Bone.LocalAxisZ = FlipPosition(Reader.ReadVec3());
        }
        if (Bone.Flags & BoneFlagExternalParent) {
            Bone.ExternalKey = Reader.ReadI32();
        }
        if (Bone.Flags & BoneFlagIk) {
            Bone.IkTargetIndex = Reader.ReadIndex(BoneIndexSize, false);
            Bone.IkLoopCount = Reader.ReadI32();
            Bone.IkLimitAngle = Reader.ReadF32();
            qint32 LinkCount = Reader.ReadI32();
            if (LinkCount < 0 || LinkCount > 1024) {
                return Fail(QStringLiteral("IK 链接"));
            }
            for (int K = 0; K < LinkCount && Reader.IsOk(); ++K) {
                PmxIkLink Link;
                Link.BoneIndex = Reader.ReadIndex(BoneIndexSize, false);
                Link.HasLimit = Reader.ReadU8() != 0;
                if (Link.HasLimit) {
                    Link.LimitMin = Reader.ReadVec3();
                    Link.LimitMax = Reader.ReadVec3();
                    FlipAngleRange(&Link.LimitMin, &Link.LimitMax);
                }
                Bone.IkLinks.append(Link);
            }
        }
    }
    if (!Reader.IsOk()) {
        return Fail(QStringLiteral("骨骼"));
    }

    // ---------- 表情 ----------
    qint32 MorphCount = Reader.ReadI32();
    if (!Reader.IsOk() || MorphCount < 0) {
        return Fail(QStringLiteral("表情"));
    }
    Morphs.resize(MorphCount);
    for (int I = 0; I < MorphCount && Reader.IsOk(); ++I) {
        PmxMorph& Morph = Morphs[I];
        Morph.Name = ReadText();
        Morph.NameEn = ReadText();
        Morph.Panel = Reader.ReadU8();
        Morph.Type = Reader.ReadU8();
        qint32 OffsetCount = Reader.ReadI32();
        if (OffsetCount < 0) {
            return Fail(QStringLiteral("表情"));
        }
        for (int K = 0; K < OffsetCount && Reader.IsOk(); ++K) {
            switch (Morph.Type) {
            case MorphTypeGroup:
            case MorphTypeFlip: {
                PmxGroupMorphOffset Item;
                Item.MorphIndex = Reader.ReadIndex(MorphIndexSize, false);
                Item.Weight = Reader.ReadF32();
                Morph.GroupOffsets.append(Item);
                break;
            }
            case MorphTypeVertex: {
                PmxVertexMorphOffset Item;
                Item.VertexIndex = Reader.ReadIndex(VertexIndexSize, true);
                Item.Offset = FlipPosition(Reader.ReadVec3());
                Morph.VertexOffsets.append(Item);
                break;
            }
            case MorphTypeBone: {
                PmxBoneMorphOffset Item;
                Item.BoneIndex = Reader.ReadIndex(BoneIndexSize, false);
                Item.Translation = FlipPosition(Reader.ReadVec3());
                QVector4D Q = Reader.ReadVec4();
                Item.Rotation = QQuaternion(Q.w(), -Q.x(), -Q.y(), Q.z()).normalized();
                Morph.BoneOffsets.append(Item);
                break;
            }
            case MorphTypeUv:
            case MorphTypeExtraUv1:
            case MorphTypeExtraUv2:
            case MorphTypeExtraUv3:
            case MorphTypeExtraUv4: {
                PmxUvMorphOffset Item;
                Item.VertexIndex = Reader.ReadIndex(VertexIndexSize, true);
                Item.Offset = Reader.ReadVec4();
                Morph.UvOffsets.append(Item);
                break;
            }
            case MorphTypeMaterial: {
                PmxMaterialMorphOffset Item;
                Item.MaterialIndex = Reader.ReadIndex(MaterialIndexSize, false);
                Item.Operation = Reader.ReadU8();
                Item.Diffuse = Reader.ReadVec4();
                Item.Specular = Reader.ReadVec3();
                Item.SpecularPower = Reader.ReadF32();
                Item.Ambient = Reader.ReadVec3();
                Item.EdgeColor = Reader.ReadVec4();
                Item.EdgeSize = Reader.ReadF32();
                Item.TextureTint = Reader.ReadVec4();
                Item.SphereTint = Reader.ReadVec4();
                Item.ToonTint = Reader.ReadVec4();
                Morph.MaterialOffsets.append(Item);
                break;
            }
            case MorphTypeImpulse:
                // 冲量表情：读取后忽略（刚体索引 + 局部标志 + 速度 + 扭矩）
                Reader.ReadIndex(RigidBodyIndexSize, false);
                Reader.Skip(1 + 12 + 12);
                break;
            default:
                return Fail(QStringLiteral("表情类型"));
            }
        }
    }
    if (!Reader.IsOk()) {
        return Fail(QStringLiteral("表情"));
    }

    // ---------- 显示枠（仅跳过） ----------
    qint32 FrameCount = Reader.ReadI32();
    for (int I = 0; I < FrameCount && Reader.IsOk(); ++I) {
        ReadText();
        ReadText();
        Reader.ReadU8();
        qint32 ElementCount = Reader.ReadI32();
        for (int K = 0; K < ElementCount && Reader.IsOk(); ++K) {
            int Target = Reader.ReadU8();
            Reader.ReadIndex(Target == 0 ? BoneIndexSize : MorphIndexSize, false);
        }
    }
    if (!Reader.IsOk()) {
        // 某些工具导出的文件在此之后被截断，此时保留已读取的数据
        return true;
    }

    // ---------- 刚体 ----------
    qint32 BodyCount = Reader.ReadI32();
    if (!Reader.IsOk() || BodyCount < 0) {
        return true;
    }
    QVector<PmxRigidBody> BodyList;
    BodyList.resize(BodyCount);
    for (int I = 0; I < BodyCount && Reader.IsOk(); ++I) {
        PmxRigidBody& Body = BodyList[I];
        Body.Name = ReadText();
        Body.NameEn = ReadText();
        Body.BoneIndex = Reader.ReadIndex(BoneIndexSize, false);
        Body.Group = Reader.ReadU8();
        Body.CollisionMask = Reader.ReadU16();
        Body.Shape = Reader.ReadU8();
        Body.Size = Reader.ReadVec3();
        Body.Position = FlipPosition(Reader.ReadVec3());
        Body.Rotation = FlipEuler(Reader.ReadVec3());
        Body.Mass = Reader.ReadF32();
        Body.LinearDamping = Reader.ReadF32();
        Body.AngularDamping = Reader.ReadF32();
        Body.Restitution = Reader.ReadF32();
        Body.Friction = Reader.ReadF32();
        Body.Mode = Reader.ReadU8();
    }
    if (!Reader.IsOk()) {
        return true;
    }
    RigidBodies = BodyList;

    // ---------- 关节 ----------
    qint32 JointCount = Reader.ReadI32();
    if (!Reader.IsOk() || JointCount < 0) {
        return true;
    }
    QVector<PmxJoint> JointList;
    JointList.resize(JointCount);
    for (int I = 0; I < JointCount && Reader.IsOk(); ++I) {
        PmxJoint& Joint = JointList[I];
        Joint.Name = ReadText();
        Joint.NameEn = ReadText();
        Joint.Type = Reader.ReadU8();
        Joint.RigidBodyA = Reader.ReadIndex(RigidBodyIndexSize, false);
        Joint.RigidBodyB = Reader.ReadIndex(RigidBodyIndexSize, false);
        Joint.Position = FlipPosition(Reader.ReadVec3());
        Joint.Rotation = FlipEuler(Reader.ReadVec3());
        Joint.LinearMin = Reader.ReadVec3();
        Joint.LinearMax = Reader.ReadVec3();
        Joint.AngularMin = Reader.ReadVec3();
        Joint.AngularMax = Reader.ReadVec3();
        Joint.LinearSpring = Reader.ReadVec3();
        Joint.AngularSpring = Reader.ReadVec3();
        FlipPositionRange(&Joint.LinearMin, &Joint.LinearMax);
        FlipAngleRange(&Joint.AngularMin, &Joint.AngularMax);
    }
    if (Reader.IsOk()) {
        Joints = JointList;
    }
    // PMX 2.1 的软体部分不支持，直接忽略
    return true;
}
