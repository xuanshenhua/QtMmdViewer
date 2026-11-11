#include "VmdMotion.h"

#include "BinaryReader.h"
#include "TextCodec.h"

#include <QFile>
#include <QFileInfo>
#include <QMap>

BezierCurve::BezierCurve()
    : X1(20.0f / 127.0f), Y1(20.0f / 127.0f), X2(107.0f / 127.0f), Y2(107.0f / 127.0f)
{
}

void BezierCurve::Set(int ByteX1, int ByteY1, int ByteX2, int ByteY2)
{
    X1 = float(ByteX1) / 127.0f;
    Y1 = float(ByteY1) / 127.0f;
    X2 = float(ByteX2) / 127.0f;
    Y2 = float(ByteY2) / 127.0f;
}

float BezierCurve::Evaluate(float X) const
{
    if (X <= 0.0f) {
        return 0.0f;
    }
    if (X >= 1.0f) {
        return 1.0f;
    }
    if (X1 == Y1 && X2 == Y2) {
        return X; // 线性
    }
    // 二分法求解参数 T，使曲线横坐标等于 X
    float Low = 0.0f;
    float High = 1.0f;
    float T = X;
    for (int Iteration = 0; Iteration < 24; ++Iteration) {
        T = (Low + High) * 0.5f;
        float InvT = 1.0f - T;
        float Bx = 3.0f * InvT * InvT * T * X1 + 3.0f * InvT * T * T * X2 + T * T * T;
        if (Bx < X) {
            Low = T;
        } else {
            High = T;
        }
    }
    float InvT = 1.0f - T;
    return 3.0f * InvT * InvT * T * Y1 + 3.0f * InvT * T * T * Y2 + T * T * T;
}

VmdMotion::VmdMotion()
    : MaxFrame(0.0f)
{
}

void VmdMotion::Clear()
{
    *this = VmdMotion();
}

bool VmdMotion::IsEmpty() const
{
    return BoneTracks.isEmpty() && MorphTracks.isEmpty() && CameraKeys.isEmpty() && IkTracks.isEmpty();
}

bool VmdMotion::Load(const QString& Path, QString* ErrorMessage)
{
    Clear();
    QFile File(Path);
    if (!File.open(QIODevice::ReadOnly)) {
        if (ErrorMessage) {
            *ErrorMessage = QStringLiteral("无法打开动作文件：%1").arg(Path);
        }
        return false;
    }
    QByteArray Data = File.readAll();
    File.close();
    FilePath = QFileInfo(Path).absoluteFilePath();

    BinaryReader Reader(Data);
    QByteArray Header = Reader.ReadBytes(30);
    if (!Header.startsWith("Vocaloid Motion Data")) {
        if (ErrorMessage) {
            *ErrorMessage = QStringLiteral("不是有效的 VMD 文件（.bvmd 为 babylon-mmd 专用格式，暂不支持）。");
        }
        return false;
    }
    int NameLength = Header.startsWith("Vocaloid Motion Data 0002") ? 20 : 10;
    ModelName = TextCodec::DecodeShiftJis(TextCodec::CutAtNull(Reader.ReadBytes(NameLength)));

    // 先放入有序映射以便按帧排序并去重
    QHash<QByteArray, QMap<quint32, VmdBoneKey>> BoneMaps;
    QHash<QByteArray, QMap<quint32, VmdMorphKey>> MorphMaps;
    QHash<QByteArray, QMap<quint32, VmdIkKey>> IkMaps;
    QMap<quint32, VmdCameraKey> CameraMap;

    // ---------- 骨骼关键帧 ----------
    if (Reader.Remaining() >= 4) {
        quint32 Count = Reader.ReadU32();
        for (quint32 I = 0; I < Count && Reader.IsOk(); ++I) {
            QByteArray Name = TextCodec::CutAtNull(Reader.ReadBytes(15));
            VmdBoneKey Key;
            Key.Frame = Reader.ReadU32();
            QVector3D Position = Reader.ReadVec3();
            QVector4D Rotation = Reader.ReadVec4();
            QByteArray Interpolation = Reader.ReadBytes(64);
            if (!Reader.IsOk()) {
                break;
            }
            // 转换到右手坐标系：平移 Z 取反，四元数 X、Y 取反
            Key.Translation = QVector3D(Position.x(), Position.y(), -Position.z());
            Key.Rotation = QQuaternion(Rotation.w(), -Rotation.x(), -Rotation.y(), Rotation.z()).normalized();
            const uchar* Bytes = reinterpret_cast<const uchar*>(Interpolation.constData());
            // 插值数据为 4 行 16 字节，第 C 行开头依次为曲线 C 的 x1、y1、x2、y2（间隔 4 字节）。
            // 第 0 行的第 2、3 字节在新版 MMD 中被用作物理开关，因此按行读取更可靠。
            for (int C = 0; C < 4; ++C) {
                Key.Curves[C].Set(Bytes[C * 16 + 0], Bytes[C * 16 + 4], Bytes[C * 16 + 8], Bytes[C * 16 + 12]);
            }
            BoneMaps[Name].insert(Key.Frame, Key);
            MaxFrame = qMax(MaxFrame, float(Key.Frame));
        }
    }

    // ---------- 表情关键帧 ----------
    if (Reader.IsOk() && Reader.Remaining() >= 4) {
        quint32 Count = Reader.ReadU32();
        for (quint32 I = 0; I < Count && Reader.IsOk(); ++I) {
            QByteArray Name = TextCodec::CutAtNull(Reader.ReadBytes(15));
            VmdMorphKey Key;
            Key.Frame = Reader.ReadU32();
            Key.Weight = Reader.ReadF32();
            if (!Reader.IsOk()) {
                break;
            }
            MorphMaps[Name].insert(Key.Frame, Key);
            MaxFrame = qMax(MaxFrame, float(Key.Frame));
        }
    }

    // ---------- 镜头关键帧 ----------
    if (Reader.IsOk() && Reader.Remaining() >= 4) {
        quint32 Count = Reader.ReadU32();
        for (quint32 I = 0; I < Count && Reader.IsOk(); ++I) {
            VmdCameraKey Key;
            Key.Frame = Reader.ReadU32();
            Key.Distance = Reader.ReadF32();
            Key.Position = Reader.ReadVec3();
            Key.Rotation = Reader.ReadVec3();
            QByteArray Interpolation = Reader.ReadBytes(24);
            Key.Fov = float(Reader.ReadU32());
            Key.Perspective = Reader.ReadU8() == 0;
            if (!Reader.IsOk()) {
                break;
            }
            const uchar* Bytes = reinterpret_cast<const uchar*>(Interpolation.constData());
            for (int C = 0; C < 6; ++C) {
                // 镜头插值布局为 x1、x2、y1、y2
                Key.Curves[C].Set(Bytes[C * 4 + 0], Bytes[C * 4 + 2], Bytes[C * 4 + 1], Bytes[C * 4 + 3]);
            }
            CameraMap.insert(Key.Frame, Key);
            MaxFrame = qMax(MaxFrame, float(Key.Frame));
        }
    }

    // ---------- 灯光与自阴影（跳过） ----------
    if (Reader.IsOk() && Reader.Remaining() >= 4) {
        quint32 Count = Reader.ReadU32();
        Reader.Skip(int(Count) * 28);
    }
    if (Reader.IsOk() && Reader.Remaining() >= 4) {
        quint32 Count = Reader.ReadU32();
        Reader.Skip(int(Count) * 9);
    }

    // ---------- IK 开关关键帧 ----------
    if (Reader.IsOk() && Reader.Remaining() >= 4) {
        quint32 Count = Reader.ReadU32();
        for (quint32 I = 0; I < Count && Reader.IsOk(); ++I) {
            quint32 Frame = Reader.ReadU32();
            Reader.ReadU8(); // 模型显示开关
            quint32 IkCount = Reader.ReadU32();
            for (quint32 K = 0; K < IkCount && Reader.IsOk(); ++K) {
                QByteArray Name = TextCodec::CutAtNull(Reader.ReadBytes(20));
                VmdIkKey Key;
                Key.Frame = Frame;
                Key.Enabled = Reader.ReadU8() != 0;
                if (Reader.IsOk()) {
                    IkMaps[Name].insert(Frame, Key);
                }
            }
        }
    }

    for (auto It = BoneMaps.constBegin(); It != BoneMaps.constEnd(); ++It) {
        BoneTracks.insert(It.key(), It.value().values().toVector());
    }
    for (auto It = MorphMaps.constBegin(); It != MorphMaps.constEnd(); ++It) {
        MorphTracks.insert(It.key(), It.value().values().toVector());
    }
    for (auto It = IkMaps.constBegin(); It != IkMaps.constEnd(); ++It) {
        IkTracks.insert(It.key(), It.value().values().toVector());
    }
    CameraKeys = CameraMap.values().toVector();
    return true;
}

namespace
{
// 按帧号合并两个已排序的关键帧序列
template <typename KeyType>
QVector<KeyType> MergeTrack(const QVector<KeyType>& First, const QVector<KeyType>& Second)
{
    QMap<quint32, KeyType> Map;
    for (const KeyType& Key : First) {
        Map.insert(Key.Frame, Key);
    }
    for (const KeyType& Key : Second) {
        Map.insert(Key.Frame, Key);
    }
    return Map.values().toVector();
}

template <typename KeyType>
void MergeTracks(QHash<QByteArray, QVector<KeyType>>* Target, const QHash<QByteArray, QVector<KeyType>>& Source)
{
    for (auto It = Source.constBegin(); It != Source.constEnd(); ++It) {
        auto Existing = Target->find(It.key());
        if (Existing == Target->end()) {
            Target->insert(It.key(), It.value());
        } else {
            Existing.value() = MergeTrack(Existing.value(), It.value());
        }
    }
}
}

void VmdMotion::Merge(const VmdMotion& Other)
{
    MergeTracks(&BoneTracks, Other.BoneTracks);
    MergeTracks(&MorphTracks, Other.MorphTracks);
    MergeTracks(&IkTracks, Other.IkTracks);
    CameraKeys = MergeTrack(CameraKeys, Other.CameraKeys);
    MaxFrame = qMax(MaxFrame, Other.MaxFrame);
}

namespace
{
// 二分查找第一个帧号大于 Frame 的关键帧下标
template <typename KeyType>
int UpperBound(const QVector<KeyType>& Track, float Frame)
{
    int Low = 0;
    int High = Track.size();
    while (Low < High) {
        int Middle = (Low + High) / 2;
        if (float(Track.at(Middle).Frame) <= Frame) {
            Low = Middle + 1;
        } else {
            High = Middle;
        }
    }
    return Low;
}
}

void VmdMotion::SampleBone(const QVector<VmdBoneKey>& Track, float Frame, QVector3D* Translation, QQuaternion* Rotation)
{
    if (Track.isEmpty()) {
        *Translation = QVector3D();
        *Rotation = QQuaternion();
        return;
    }
    int Index = UpperBound(Track, Frame);
    if (Index == 0) {
        *Translation = Track.first().Translation;
        *Rotation = Track.first().Rotation;
        return;
    }
    if (Index >= Track.size()) {
        *Translation = Track.last().Translation;
        *Rotation = Track.last().Rotation;
        return;
    }
    const VmdBoneKey& Previous = Track.at(Index - 1);
    const VmdBoneKey& Next = Track.at(Index);
    float Span = float(Next.Frame - Previous.Frame);
    float T = Span > 0.0f ? (Frame - float(Previous.Frame)) / Span : 1.0f;
    float Tx = Next.Curves[0].Evaluate(T);
    float Ty = Next.Curves[1].Evaluate(T);
    float Tz = Next.Curves[2].Evaluate(T);
    float Tr = Next.Curves[3].Evaluate(T);
    QVector3D A = Previous.Translation;
    QVector3D B = Next.Translation;
    *Translation = QVector3D(A.x() + (B.x() - A.x()) * Tx,
                             A.y() + (B.y() - A.y()) * Ty,
                             A.z() + (B.z() - A.z()) * Tz);
    *Rotation = QQuaternion::slerp(Previous.Rotation, Next.Rotation, Tr);
}

float VmdMotion::SampleMorph(const QVector<VmdMorphKey>& Track, float Frame)
{
    if (Track.isEmpty()) {
        return 0.0f;
    }
    int Index = UpperBound(Track, Frame);
    if (Index == 0) {
        return Track.first().Weight;
    }
    if (Index >= Track.size()) {
        return Track.last().Weight;
    }
    const VmdMorphKey& Previous = Track.at(Index - 1);
    const VmdMorphKey& Next = Track.at(Index);
    float Span = float(Next.Frame - Previous.Frame);
    float T = Span > 0.0f ? (Frame - float(Previous.Frame)) / Span : 1.0f;
    return Previous.Weight + (Next.Weight - Previous.Weight) * T;
}

bool VmdMotion::SampleIk(const QVector<VmdIkKey>& Track, float Frame)
{
    if (Track.isEmpty()) {
        return true;
    }
    int Index = UpperBound(Track, Frame);
    if (Index == 0) {
        return Track.first().Enabled;
    }
    return Track.at(Index - 1).Enabled;
}

bool VmdMotion::SampleCamera(float Frame, CameraPose* Pose) const
{
    if (CameraKeys.isEmpty()) {
        return false;
    }
    int Index = UpperBound(CameraKeys, Frame);
    const VmdCameraKey* Previous = nullptr;
    const VmdCameraKey* Next = nullptr;
    if (Index == 0) {
        Previous = &CameraKeys.first();
        Next = Previous;
    } else if (Index >= CameraKeys.size()) {
        Previous = &CameraKeys.last();
        Next = Previous;
    } else {
        Previous = &CameraKeys.at(Index - 1);
        Next = &CameraKeys.at(Index);
    }
    float Span = float(Next->Frame) - float(Previous->Frame);
    // 相邻帧（间隔为 1）视为镜头切换，不做插值
    if (Previous == Next || Span <= 1.0f) {
        Pose->Center = Previous->Position;
        Pose->Rotation = Previous->Rotation;
        Pose->Distance = Previous->Distance;
        Pose->Fov = Previous->Fov;
        return true;
    }
    float T = (Frame - float(Previous->Frame)) / Span;
    float Tx = Next->Curves[0].Evaluate(T);
    float Ty = Next->Curves[1].Evaluate(T);
    float Tz = Next->Curves[2].Evaluate(T);
    float Tr = Next->Curves[3].Evaluate(T);
    float Td = Next->Curves[4].Evaluate(T);
    float Tf = Next->Curves[5].Evaluate(T);
    QVector3D A = Previous->Position;
    QVector3D B = Next->Position;
    Pose->Center = QVector3D(A.x() + (B.x() - A.x()) * Tx, A.y() + (B.y() - A.y()) * Ty, A.z() + (B.z() - A.z()) * Tz);
    Pose->Rotation = Previous->Rotation + (Next->Rotation - Previous->Rotation) * Tr;
    Pose->Distance = Previous->Distance + (Next->Distance - Previous->Distance) * Td;
    Pose->Fov = Previous->Fov + (Next->Fov - Previous->Fov) * Tf;
    return true;
}
