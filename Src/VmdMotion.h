#ifndef VMDMOTION_H
#define VMDMOTION_H

#include <QByteArray>
#include <QHash>
#include <QQuaternion>
#include <QString>
#include <QVector>
#include <QVector3D>

// 三次贝塞尔插值曲线，端点固定为 (0,0) 与 (1,1)
struct BezierCurve
{
    float X1;
    float Y1;
    float X2;
    float Y2;

    BezierCurve();
    void Set(int ByteX1, int ByteY1, int ByteX2, int ByteY2);
    float Evaluate(float X) const;
};

struct VmdBoneKey
{
    quint32 Frame;
    QVector3D Translation;
    QQuaternion Rotation;
    BezierCurve Curves[4]; // X、Y、Z 平移与旋转
};

struct VmdMorphKey
{
    quint32 Frame;
    float Weight;
};

struct VmdCameraKey
{
    quint32 Frame;
    float Distance;
    QVector3D Position;
    QVector3D Rotation;
    BezierCurve Curves[6]; // X、Y、Z、旋转、距离、视角
    float Fov;
    bool Perspective;
};

struct VmdIkKey
{
    quint32 Frame;
    bool Enabled;
};

// 镜头采样结果（保持 MMD 原始数值，由渲染器转换为右手坐标系）
struct CameraPose
{
    QVector3D Center;
    QVector3D Rotation;
    float Distance;
    float Fov;
};

// VMD 动作文件：骨骼、表情、镜头与 IK 开关关键帧。
// 骨骼与表情名称以 Shift-JIS 原始字节作为键，便于与模型名称精确匹配。
class VmdMotion
{
public:
    VmdMotion();

    bool Load(const QString& FilePath, QString* ErrorMessage);
    void Clear();
    // 合并另一个动作的关键帧（同一轨道同一帧以后者为准）
    void Merge(const VmdMotion& Other);
    bool IsEmpty() const;
    bool HasCamera() const { return !CameraKeys.isEmpty(); }
    bool HasBodyMotion() const { return !BoneTracks.isEmpty() || !MorphTracks.isEmpty(); }

    static void SampleBone(const QVector<VmdBoneKey>& Track, float Frame, QVector3D* Translation, QQuaternion* Rotation);
    static float SampleMorph(const QVector<VmdMorphKey>& Track, float Frame);
    static bool SampleIk(const QVector<VmdIkKey>& Track, float Frame);
    bool SampleCamera(float Frame, CameraPose* Pose) const;

    QString FilePath;
    QString ModelName;
    float MaxFrame;
    QHash<QByteArray, QVector<VmdBoneKey>> BoneTracks;
    QHash<QByteArray, QVector<VmdMorphKey>> MorphTracks;
    QHash<QByteArray, QVector<VmdIkKey>> IkTracks;
    QVector<VmdCameraKey> CameraKeys;
};

#endif
